#include "ota_ferced.h"

// Arduino.h va primero: data/le_roots.h usa PROGMEM y no lo trae por su cuenta,
// asi que incluirlo antes deja el header sin compilar.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>
#include <esp_heap_caps.h>
#include <cstdlib>
#include <cstring>

#include "ferced_config.h"
#include "data/le_roots.h"

// Las tres macros las inyecta tools/inject_version.py en los entornos de
// Ferced. El fallback es solo para que el modulo compile en los entornos que
// no corren ese script; ahi el OTA no se usa.
#ifndef FERCED_VERSION
#define FERCED_VERSION "0.0.0"
#endif

// 4 KB por vuelta: suficiente para no hacer una escritura de flash por cada
// paquete TCP, y chico al lado del MB que baja.
static const size_t BUF_LEN = 4096;

// Un binario de ~1 MB por WiFi tarda; el corte tiene que ser por falta de
// datos, no por lentitud.
static const uint32_t ESPERA_DATOS_MS = 15000;

static const size_t SHA_HEX_LEN = 64;

// -- URLs ------------------------------------------------------------------
// El firmware sale del mismo proxy que el feed, asi que la base se deduce de
// FEED_ENDPOINT en vez de configurarse aparte: una sola URL para cambiar si el
// aparato se apunta a otro servidor. Es el mismo recorte que hace
// feed_client.cpp para armar /v1/img.
static bool urlFirmware(char* out, size_t len, bool binario) {
    const char* base = FEED_ENDPOINT;
    const char* corte = strstr(base, "/v1/feed");
    if (!corte) return false;
    snprintf(out, len, "%.*s/v1/firmware%s",
             (int)(corte - base), base, binario ? "/bin" : "");
    return true;
}

// -- Conexion --------------------------------------------------------------
// El punto de todo el modulo: sobre https se valida el certificado contra las
// raices embebidas. NUNCA setInsecure() aca. El feed puede permitirselo porque
// baja texto que ademas se muestra tal cual; esto baja codigo que el aparato
// va a ejecutar despues de reiniciar, y sin validar cualquiera en el camino
// puede decidir cual.
static bool abrirProxy(const char* url, WiFiClient& plano, WiFiClientSecure& tls,
                       HTTPClient& http) {
    const bool seguro = strncmp(url, "https://", 8) == 0;
    if (seguro) tls.setCACert(LE_ROOTS);

    http.setTimeout(15000);
    http.setUserAgent("ferced-display-ota/1.0");
    if (!(seguro ? http.begin(tls, url) : http.begin(plano, url))) return false;
    if (sizeof(FEED_TOKEN) > 1) {
        http.addHeader("Authorization", "Bearer " FEED_TOKEN);
    }
    return true;
}

// Lo que HTTPClient devuelve cuando falla el TLS es -1, y -1 no dice nada: es
// el mismo numero para "no hay ruta al host" que para "el certificado no
// valida". mbedTLS SI sabe cual de las dos fue, y WiFiClientSecure lo guarda;
// hay que ir a buscarlo a mano.
//
// Existe porque se perdio una tarde entera con esto: en pantalla decia "no se
// pudo conectar o el TLS fallo" —que es literalmente las dos cosas a la vez— y
// no habia forma de saber cual sin poder reproducirlo. El heap va al lado
// porque el handshake con validacion de cadena es de los picos de memoria mas
// grandes del firmware, y una cadena de cuatro certificados es bastante mas de
// lo que habia cuando esto se escribio.
static void porQueFallo(WiFiClientSecure& tls, int code) {
    char err[128] = {0};
    const int mb = tls.lastError(err, sizeof(err));
    Serial.printf("[OTA] fallo: http=%d mbedtls=%d (-0x%04X) \"%s\"\n",
                  code, mb, mb < 0 ? -mb : mb, err[0] ? err : "sin detalle");
    Serial.printf("[OTA] heap libre %u, bloque mas grande %u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    // -0x2700 es X509_CERT_VERIFY_FAILED: la cadena no valida contra las raices
    // embebidas. Si aparece eso, regenerar con tools/fetch_le_roots.py, que ya
    // trae tambien las raices de la generacion Y.
}

// Traduce el codigo a algo que se entienda mirando la pantalla. -1 y -11 son
// los de HTTPClient (conexion rechazada y timeout), no del servidor.
static void explicarHTTP(char* detalle, size_t len, int code) {
    switch (code) {
        case HTTP_CODE_NOT_FOUND:
            snprintf(detalle, len, "el proxy no tiene firmware (404)");
            break;
        case HTTP_CODE_UNAUTHORIZED:
            snprintf(detalle, len, "el proxy rechazo el token (401)");
            break;
        case HTTPC_ERROR_CONNECTION_REFUSED:
            snprintf(detalle, len, "no se pudo conectar o el TLS fallo");
            break;
        case HTTPC_ERROR_READ_TIMEOUT:
            snprintf(detalle, len, "el servidor no respondio a tiempo");
            break;
        default:
            snprintf(detalle, len, "el proxy respondio HTTP %d", code);
            break;
    }
}

// -- Comparacion de versiones ----------------------------------------------
// Copiado de ota_manager.cpp a proposito: ese archivo esta afuera del build de
// Ferced porque arrastra ROOT_CAS desde api_client.cpp, que tambien lo esta.
// Son quince lineas; incluirlo costaria mas que repetirlo.
//
// Acepta "M.m.p" y "M.m.p-beta.N" / "M.m.p-rcN". Una version estable, sin
// sufijo, vale INT_MAX como prerelease para que le gane a cualquier beta del
// mismo M.m.p.
static void parseVersion(const char* s, int& maj, int& min, int& pat, int& pre) {
    maj = min = pat = 0;
    pre = 0x7FFFFFFF;
    if (!s) return;
    sscanf(s, "%d.%d.%d", &maj, &min, &pat);
    const char* dash = strchr(s, '-');
    if (!dash) return;
    const char* p = dash + 1;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return;
    int n = 0;
    sscanf(p, "%d", &n);
    pre = n;
}

static bool esNueva(const char* remota, const char* local) {
    int rMaj, rMin, rPat, rPre;
    int lMaj, lMin, lPat, lPre;
    parseVersion(remota, rMaj, rMin, rPat, rPre);
    parseVersion(local, lMaj, lMin, lPat, lPre);
    if (rMaj != lMaj) return rMaj > lMaj;
    if (rMin != lMin) return rMin > lMin;
    if (rPat != lPat) return rPat > lPat;
    return rPre > lPre;
}

// -- sha256 en streaming ---------------------------------------------------
// El binario no entra en RAM: es ~1 MB contra 320 KB. El hash se calcula sobre
// los mismos bytes que se escriben en flash y en el mismo paso, asi que no hay
// un "despues" en el que verificarlo. Envuelto en un objeto para que ningun
// camino de error se olvide de liberar el contexto.
struct OtaSha256 {
    mbedtls_sha256_context ctx;

    OtaSha256() {
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts_ret(&ctx, 0);  // 0 = sha256, no sha224
    }
    ~OtaSha256() { mbedtls_sha256_free(&ctx); }

    void agregar(const uint8_t* datos, size_t n) {
        mbedtls_sha256_update_ret(&ctx, datos, n);
    }

    void hex(char out[SHA_HEX_LEN + 1]) {
        unsigned char dig[32];
        mbedtls_sha256_finish_ret(&ctx, dig);
        static const char H[] = "0123456789abcdef";
        for (size_t i = 0; i < sizeof(dig); i++) {
            out[i * 2] = H[dig[i] >> 4];
            out[i * 2 + 1] = H[dig[i] & 0x0F];
        }
        out[SHA_HEX_LEN] = '\0';
    }
};

// El proxy manda el hash en minusculas, pero comparar sin distinguir mayusculas
// evita que un cambio de formato del lado del servidor parezca un binario
// corrupto.
static bool mismoHex(const char* a, const char* b) {
    for (size_t i = 0; i < SHA_HEX_LEN; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'F') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'F') cb = (char)(cb - 'A' + 'a');
        if (ca != cb || ca == '\0') return false;
    }
    return a[SHA_HEX_LEN] == '\0' && b[SHA_HEX_LEN] == '\0';
}

// El buffer va al heap: 4 KB en la pila de loop() es demasiado, y en estatico
// serian 4 KB reservados para siempre por algo que corre una vez cada tanto.
struct OtaBuffer {
    uint8_t* p;
    explicit OtaBuffer(size_t n) : p((uint8_t*)malloc(n)) {}
    ~OtaBuffer() { free(p); }
    OtaBuffer(const OtaBuffer&) = delete;
    OtaBuffer& operator=(const OtaBuffer&) = delete;
};

// -- Metadata --------------------------------------------------------------
// Pide /v1/firmware. La devuelve otaBuscar para comparar y tambien otaAplicar
// justo antes de bajar: releerla asegura que el hash corresponda al binario
// que esta en el VPS en ese momento, y no a uno que se reemplazo entre que se
// consulto y se toco el boton.
static bool pedirMeta(char* version, size_t versionLen,
                      char* sha, size_t shaLen,
                      uint32_t* size,
                      char* detalle, size_t detalleLen) {
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(detalle, detalleLen, "sin WiFi");
        return false;
    }

    char url[192];
    if (!urlFirmware(url, sizeof(url), false)) {
        snprintf(detalle, detalleLen, "el endpoint no tiene /v1/feed");
        return false;
    }

    WiFiClient plano;
    WiFiClientSecure tls;
    HTTPClient http;
    if (!abrirProxy(url, plano, tls, http)) {
        snprintf(detalle, detalleLen, "no se pudo abrir la conexion");
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        explicarHTTP(detalle, detalleLen, code);
        porQueFallo(tls, code);
        http.end();
        return false;
    }

    // getString() desarma el framing de chunks; el stream crudo lo dejaria
    // adentro del texto. Es la misma trampa que ya mordio al feed.
    const String payload = http.getString();
    http.end();

    if (payload.isEmpty()) {
        snprintf(detalle, detalleLen, "el proxy no devolvio nada");
        return false;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        snprintf(detalle, detalleLen, "metadata ilegible: %s", err.c_str());
        return false;
    }

    const char* v = doc["version"] | "";
    const char* h = doc["sha256"] | "";
    const uint32_t n = doc["size"] | 0UL;

    if (!v[0] || strlen(h) != SHA_HEX_LEN || n == 0) {
        snprintf(detalle, detalleLen, "metadata incompleta");
        return false;
    }

    snprintf(version, versionLen, "%s", v);
    snprintf(sha, shaLen, "%s", h);
    *size = n;
    return true;
}

OtaCheck otaBuscar() {
    OtaCheck r = {};

    char version[16] = {0};
    char sha[SHA_HEX_LEN + 1] = {0};
    uint32_t size = 0;

    if (!pedirMeta(version, sizeof(version), sha, sizeof(sha), &size,
                   r.detalle, sizeof(r.detalle))) {
        r.error = true;
        Serial.printf("[OTA] no pude consultar la version: %s\n", r.detalle);
        return r;
    }

    snprintf(r.version, sizeof(r.version), "%s", version);
    r.size = size;
    r.hayNueva = esNueva(version, FERCED_VERSION);

    Serial.printf("[OTA] el proxy tiene %s, aca corre %s -> %s\n",
                  version, FERCED_VERSION, r.hayNueva ? "hay nueva" : "al dia");
    if (!r.hayNueva) {
        snprintf(r.detalle, sizeof(r.detalle), "ya esta al dia");
    }
    return r;
}

// -- Descarga --------------------------------------------------------------
// Espera a que el stream tenga algo. Sin esto un corte de red deja el bucle
// girando para siempre con la particion a medio escribir.
static bool esperarDatos(WiFiClient* s) {
    const uint32_t t0 = millis();
    while (s->available() == 0 && millis() - t0 < ESPERA_DATOS_MS) {
        delay(10);
        esp_task_wdt_reset();
    }
    return s->available() > 0;
}

bool otaAplicar(void (*progreso)(int pct), char* detalle, size_t detalleLen) {
    // Si no dan buffer se escribe igual en uno propio: el resto del codigo no
    // tiene que preguntarse si puede explicar lo que pasa.
    char propio[64];
    if (!detalle || detalleLen == 0) {
        detalle = propio;
        detalleLen = sizeof(propio);
    }
    detalle[0] = '\0';

    char version[16] = {0};
    char sha[SHA_HEX_LEN + 1] = {0};
    uint32_t esperado = 0;
    if (!pedirMeta(version, sizeof(version), sha, sizeof(sha), &esperado,
                   detalle, detalleLen)) {
        Serial.printf("[OTA] %s\n", detalle);
        return false;
    }

    char url[192];
    if (!urlFirmware(url, sizeof(url), true)) {
        snprintf(detalle, detalleLen, "el endpoint no tiene /v1/feed");
        return false;
    }

    WiFiClient plano;
    WiFiClientSecure tls;
    HTTPClient http;
    if (!abrirProxy(url, plano, tls, http)) {
        snprintf(detalle, detalleLen, "no se pudo abrir la conexion");
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        explicarHTTP(detalle, detalleLen, code);
        porQueFallo(tls, code);
        http.end();
        return false;
    }

    // Update.h necesita saber de antemano cuanto va a escribir. El proxy
    // declara Content-Length justamente para esto; si no coincide con lo que
    // dijo la metadata, algo cambio en el medio y no se toca el flash.
    const int declarado = http.getSize();
    if (declarado <= 0 || (uint32_t)declarado != esperado) {
        snprintf(detalle, detalleLen, "largo %d, la version dice %lu",
                 declarado, (unsigned long)esperado);
        http.end();
        return false;
    }

    if (!Update.begin((size_t)declarado)) {
        snprintf(detalle, detalleLen, "no arranca el flash: %s", Update.errorString());
        http.end();
        return false;
    }

    OtaBuffer buf(BUF_LEN);
    if (!buf.p) {
        snprintf(detalle, detalleLen, "sin memoria para el buffer");
        Update.abort();
        http.end();
        return false;
    }

    // Todo lo que sale por aca deja el motivo escrito y la particion abortada:
    // una actualizacion a medias que no se cancela puede quedar marcada como
    // arrancable.
    auto fallar = [&](const char* texto) {
        snprintf(detalle, detalleLen, "%s", texto);
        Serial.printf("[OTA] %s\n", detalle);
        Update.abort();
        http.end();
        return false;
    };

    Serial.printf("[OTA] bajando %s, %d bytes\n", version, declarado);

    OtaSha256 hash;
    WiFiClient* stream = http.getStreamPtr();
    size_t escrito = 0;
    int ultimoPct = -1;
    if (progreso) progreso(0);

    while (escrito < (size_t)declarado) {
        const size_t hay = stream->available();
        if (hay == 0) {
            if (!esperarDatos(stream)) return fallar("la descarga se quedo sin datos");
            continue;
        }

        const size_t pedir = hay < BUF_LEN ? hay : BUF_LEN;
        const int leido = stream->readBytes(buf.p, pedir);
        if (leido <= 0) return fallar("la descarga se corto");

        hash.agregar(buf.p, (size_t)leido);
        if (Update.write(buf.p, (size_t)leido) != (size_t)leido) {
            return fallar("fallo la escritura en flash");
        }
        escrito += (size_t)leido;

        // Quien reciba el progreso va a repintar pantalla, y eso cuesta ~99 ms:
        // un aviso por punto porcentual, no uno por bloque leido.
        const int pct = (int)((escrito * 100) / (size_t)declarado);
        if (pct != ultimoPct) {
            ultimoPct = pct;
            esp_task_wdt_reset();
            if (progreso) progreso(pct);
        }
    }

    if (escrito != (size_t)declarado) {
        return fallar("la descarga llego incompleta");
    }

    char calculado[SHA_HEX_LEN + 1];
    hash.hex(calculado);
    if (!mismoHex(calculado, sha)) {
        Serial.printf("[OTA] sha256 esperado %s, calculado %s\n", sha, calculado);
        return fallar("el sha256 no coincide, firmware descartado");
    }

    // Recien aca. Update.end() marca la particion nueva como la de arranque:
    // llamarlo antes de verificar seria dejar el aparato apuntando a un binario
    // que no se sabe que es.
    if (!Update.end()) {
        snprintf(detalle, detalleLen, "no cierra el flash: %s", Update.errorString());
        Serial.printf("[OTA] %s\n", detalle);
        http.end();
        return false;
    }
    http.end();

    if (!Update.isFinished()) {
        snprintf(detalle, detalleLen, "la escritura no termino");
        Serial.printf("[OTA] %s\n", detalle);
        return false;
    }

    Serial.printf("[OTA] %s escrita y verificada, reiniciando\n", version);
    Serial.flush();
    delay(300);  // que salga el ultimo log y la pantalla muestre el 100%
    ESP.restart();
    return true;  // no se llega: ESP.restart() no vuelve
}

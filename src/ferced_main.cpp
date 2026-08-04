#include <Arduino.h>
#include <cstring>
#include <ctime>
#include <esp_task_wdt.h>

#include "colors.h"
#include "display_manager.h"
#include "feed_client.h"
#include "ferced_config.h"
#include "nvs_storage.h"
#include "time_manager.h"
#include "touch_manager.h"
#include "ui_config.h"
#include "ui_ferced.h"
#include "wifi_manager.h"
#include "wifi_provision.h"

enum AppPhase : uint8_t {
    PHASE_PROVISION,
    PHASE_CONNECTING,
    PHASE_RUNNING,
    PHASE_CONFIG,
};

static AppPhase s_phase = PHASE_PROVISION;
static uint8_t  s_index = 0;
static uint32_t s_lastRotate = 0;
static uint32_t s_lastFetch = 0;
static uint32_t s_retryMs = FEED_RETRY_MIN_MS;
static uint32_t s_nextRetry = 0;
static bool     s_offline = false;

#define WIFI_RETRY_MIN_MS  5000UL
#define WIFI_RETRY_MAX_MS 60000UL
static uint32_t s_wifiRetryMs = WIFI_RETRY_MIN_MS;
static uint32_t s_nextWifiRetry = 0;

// NTP deja el reloj del sistema en epoch UTC, igual que el campo ts del feed.
// Sin sincronizar devuelve 0 y la UI cae en "recien".
static uint32_t nowEpoch() {
    if (!timeReady()) return 0;
    return (uint32_t)time(nullptr);
}

static void startProvisioning() {
    s_phase = PHASE_PROVISION;
    provisionStart();
    provisionDrawQR();
}

static void showCurrent() {
    const uint8_t total = feedCount();
    if (total == 0) {
        uiFercedShowStatus("Sin contenido", "No llego nada del feed. Tocar para reintentar.");
        return;
    }
    if (s_index >= total) s_index = 0;
    uiFercedShowItem(feedItem(s_index), s_index, total, s_offline);
}

static void refreshFeed() {
    const UiFrameStats st = uiFercedStats();
    const FeedResult r = feedFetch(st.frames, st.avgUs100, st.worstUs100);
    s_lastFetch = millis();

    if (r == FEED_UPDATED) {
        s_offline = false;
        s_retryMs = FEED_RETRY_MIN_MS;
        s_nextRetry = 0;
        s_index = 0;
        showCurrent();
        s_lastRotate = millis();
        return;
    }

    // Falla de red: se conserva el pool y se reintenta con backoff, en vez de
    // dejar la pantalla vacia.
    s_offline = true;
    s_nextRetry = millis() + s_retryMs;
    s_retryMs = s_retryMs * 2 > FEED_RETRY_MAX_MS ? FEED_RETRY_MAX_MS : s_retryMs * 2;
    showCurrent();
}

static void enterRunning() {
    s_phase = PHASE_RUNNING;
    timeSetup();
    uiFercedShowStatus("Conectado", "Buscando contenido.");
    refreshFeed();
    s_lastRotate = millis();
}

static void advance() {
    const uint8_t total = feedCount();
    if (total == 0) { refreshFeed(); return; }
    s_index = (s_index + 1) % total;
    showCurrent();
    s_lastRotate = millis();
}

// El host del endpoint, sin esquema ni ruta: alcanza para distinguir el build
// de LAN del de produccion y no expone la ruta ni el token.
static const char* endpointHost() {
    static char host[48];
    const char* p = strstr(FEED_ENDPOINT, "://");
    p = p ? p + 3 : FEED_ENDPOINT;
    size_t n = 0;
    while (p[n] && p[n] != '/' && n < sizeof(host) - 1) { host[n] = p[n]; n++; }
    host[n] = '\0';
    return host;
}

// La pantalla de diagnostico no anima nada: se dibuja una sola vez al entrar y
// el loop de la fase se limita a esperar un toque.
static void enterConfig() {
    s_phase = PHASE_CONFIG;

    // wifiSSID() devuelve el SSID con el que se conecto, sin tener que leer la
    // clave de NVS para nada.
    const bool online = wifiConnected();
    const String ip = wifiIP();

    // OJO: avgUs100 es el costo de pintar un frame, no el periodo entre
    // frames. El fps real sale del periodo, que ui_ferced.cpp promedia para su
    // log [anim] pero no publica en UiFrameStats. Hasta que lo publique, este
    // numero es una cota superior y da mas alto que lo que se ve en pantalla.
    const UiFrameStats st = uiFercedStats();
    const float fps = st.avgUs100 > 0 ? 10000.0f / (float)st.avgUs100 : 0.0f;

    const ConfigInfo info = {
        FERCED_VERSION, FERCED_COMMIT, FERCED_BUILD_DATE,
        wifiSSID(), ip.c_str(), endpointHost(),
        millis() / 1000,
        fps,
        feedCount(),
        online,
    };
    uiConfigDraw(info);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ferced-display ===");

    nvsInit();
    Colors::setTheme(Colors::THEME_DARK);
    displaySetup();
    displaySetupVSync();
    displaySetBrightness(nvsGetBrightness());
    touchSetup();
    uiFercedSetup();

    if (!nvsHasWifi()) {
        uiFercedShowStatus("Configurar", "Escanea el codigo para conectar el equipo a tu red.");
        startProvisioning();
        return;
    }

    char ssid[33] = {};
    char pass[65] = {};
    nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));

    uiFercedShowStatus("Conectando", ssid);
    wifiSetup(ssid, pass);

    if (!wifiConnected()) {
        uiFercedShowStatus("Sin red", "Reintentando sola.");
        s_phase = PHASE_CONNECTING;
        return;
    }
    enterRunning();
}

void loop() {
    esp_task_wdt_reset();
    const uint32_t now = millis();

    if (s_phase == PHASE_PROVISION) {
        if (provisionTick()) {
            char ssid[33] = {};
            char pass[65] = {};
            provisionGetCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
            provisionStop();

            uiFercedShowStatus("Probando", ssid);
            wifiSetup(ssid, pass);
            if (wifiConnected()) {
                nvsSaveWifi(ssid, pass);
                enterRunning();
            } else {
                uiFercedShowStatus("No anduvo", "Esa red no conecto. Probemos de nuevo.");
                startProvisioning();
            }
        }
        delay(4);
        return;
    }

    if (s_phase == PHASE_CONNECTING) {
        // Reintenta sola. Un aparato de escritorio no puede quedarse esperando
        // que alguien lo toque porque el router tardo en levantar.
        if (touchLoop().gesture == TOUCH_TAP) {
            startProvisioning();
            return;
        }
        if (now >= s_nextWifiRetry) {
            char ssid[33] = {};
            char pass[65] = {};
            nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));
            wifiSetup(ssid, pass);

            if (wifiConnected()) {
                s_wifiRetryMs = WIFI_RETRY_MIN_MS;
                enterRunning();
                return;
            }
            uiFercedShowStatus("Sin red", "No conecta. Tocar para reconfigurar.");
            s_nextWifiRetry = millis() + s_wifiRetryMs;
            s_wifiRetryMs = s_wifiRetryMs * 2 > WIFI_RETRY_MAX_MS
                                ? WIFI_RETRY_MAX_MS
                                : s_wifiRetryMs * 2;
        }
        uiFercedTick(nowEpoch(), 0.0f);
        delay(8);
        return;
    }

    if (s_phase == PHASE_CONFIG) {
        // La pantalla esta quieta: no hay nada que repintar, solo resolver el
        // toque contra la geometria de los botones, que vive en ui_config.cpp.
        const TouchEvent ev = touchLoop();
        if (ev.gesture == TOUCH_TAP) {
            switch (uiConfigHit(ev.x, ev.y)) {
                case CFG_CLOSE:
                    s_phase = PHASE_RUNNING;
                    showCurrent();
                    s_lastRotate = millis();
                    break;
                case CFG_REFRESH:
                    // refreshFeed() ya repinta y reacomoda s_lastRotate cuando
                    // baja contenido nuevo; el reset de aca cubre el caso en
                    // que la red falle y se quede con el pool anterior.
                    s_phase = PHASE_RUNNING;
                    refreshFeed();
                    s_lastRotate = millis();
                    break;
                case CFG_FORGET:
                    // Sin confirmacion, por decision explicita. El boton va
                    // separado y en rojo para bajar la chance de un roce.
                    nvsForgetWifi();
                    ESP.restart();
                    break;
                default:
                    break;
            }
        }
        delay(8);
        return;
    }

    wifiLoop();

    // touchLoop() consume el evento: una sola llamada por vuelta y se reparte
    // el resultado, porque la segunda ya devolveria TOUCH_NONE.
    const TouchEvent ev = touchLoop();
    if (ev.gesture == TOUCH_LONG_PRESS) { enterConfig(); return; }
    if (ev.gesture == TOUCH_TAP) advance();

    const uint32_t sinceRotate = now - s_lastRotate;
    if (sinceRotate >= FEED_ROTATE_MS) advance();

    const bool dueRefresh = now - s_lastFetch >= FEED_REFRESH_MS;
    const bool dueRetry = s_offline && s_nextRetry != 0 && now >= s_nextRetry;
    if (dueRefresh || dueRetry) refreshFeed();

    // La animacion la marca el VSync dentro de uiFercedTick; el delay solo
    // evita que el loop queme CPU cuando no hay nada que repintar.
    const bool busy = uiFercedTick(nowEpoch(),
                                   (float)sinceRotate / (float)FEED_ROTATE_MS);
    delay(busy ? 1 : 8);
}

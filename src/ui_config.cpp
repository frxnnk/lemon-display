#include "ui_config.h"

#include "display_manager.h"
#include "design_system.h"
#include "ui_chrome.h"
#include "config.h"
#include "nvs_storage.h"

#include <cstddef>
#include <cstdio>

using namespace FercedColors;

// Se compone sobre el sprite y se revela con uiAnimReveal(), como el resto.
//
// Los datos son los mismos que antes —no se fue ninguno— pero dejaron de ser
// una lista de etiqueta y valor para agruparse por tema y repartirse en dos
// columnas. Con una sola columna la pantalla se leia como un volcado de
// diagnostico: nueve renglones iguales, todos con el mismo peso, sin decir cual
// mirar primero. Ahora el valor pesa mas que su etiqueta, y los tres numeros
// que se mueven mientras la mirás —el encendido, el framerate, los items— estan
// en la cursiva con gracias de la marca y en un bloque aparte.

namespace {

LGFX_Sprite& g = uiSprite;

struct Rect { int16_t x, y, w, h; };

constexpr int MARGEN    = 32;
constexpr int ANCHO     = SCREEN_W - 2 * MARGEN;   // 416
constexpr int RADIO     = DS::RADIUS_MD;           // 12
// BTN_H_LG (50) no entra dos veces junto con la franja de estado. BTN_H_MD son
// 44, que sigue siendo el minimo tactil que fija el design system.
constexpr int BTN_H     = DS::BTN_H_MD;            // 44

// ── Encabezado ──
constexpr int MARK_Y    = 20;
constexpr int RULE_Y    = 100;

// ── Bloque de datos, en dos columnas ──
// El valor mas ancho es "2026-08-05 04:20" (150 px en Satoshi12) y el mas largo
// de la derecha "feed.ferced.com" (128). Con 196 px de columna sobran los dos y
// queda una canaleta de 24 px, que es lo que separa sin necesitar una regla.
constexpr int COL_W     = 196;
constexpr int COL1_X    = MARGEN;
constexpr int COL2_X    = MARGEN + COL_W + 24;
constexpr int DATO_Y0   = 118;
constexpr int DATO_LH   = 25;
constexpr int SEP_Y     = 222;

// ── Bloque de lo que se mueve, en tres columnas ──
// Los anchos no son tercios iguales, y estan medidos con tools/medir_texto.py
// sobre GeorgiaItalic28: "2 h 14 min" son 161 px, "35.4 fps" 121 y "20" 41. Con
// 416/3 el encendido salia recortado a "2 h 14 m..." mientras al lado sobraban
// noventa pixeles de aire. Los margenes que quedan cubren el peor caso de cada
// uno: "999 h 59 min" mide 204 y "999" mide 60.
constexpr int VIVO_Y    = 244;
constexpr int VIVO_X[3] = { MARGEN, MARGEN + 214, MARGEN + 348 };
constexpr int VIVO_W[3] = { 206, 126, 68 };

// Cuatro botones en dos filas de dos. Las columnas parten los 416 px en
// 248 + 152 con 16 de aire y agrupan por consecuencia: a la izquierda lo que
// sale a la red, a la derecha lo que corta.
//
// El corte esta medido, no elegido a ojo. Con SatoshiMedium18 los rotulos
// miden 162 ("Actualizar feed"), 217 ("Buscar actualización"), 69 ("Cerrar")
// y 110 ("Reaparear"): la columna ancha les deja 15 px de aire al mas largo y
// la angosta 21. "Reaparear WiFi" media 163 y se desbordaba de la pastilla
// angosta; el rotulo perdio el sustantivo, no el verbo, que es lo que nombra
// la accion.
constexpr Rect BTN_REFRESH = { MARGEN, 368, 248, BTN_H };
constexpr Rect BTN_CLOSE   = { 296,    368, 152, BTN_H };
constexpr Rect BTN_UPDATE  = { MARGEN, 422, 248, BTN_H };
constexpr Rect BTN_FORGET  = { 296,    422, 152, BTN_H };

// La franja de estado OCUPA el bloque de los numeros vivos.
//
// Antes vivia sola en 322, entre el bloque de datos y los botones: un renglon
// chico y sin rotulo flotando en el aire, que ademas quedaba pegado a los
// botones y se leia como parte de ellos. Y era lo mas importante que podia
// pasar en la pantalla dicho en el cuerpo mas chico que hay.
//
// Que se ponga encima del bloque vivo no es un truco de espacio: ese bloque es
// justamente "lo que se mueve mientras la mirás", y mientras baja una
// actualizacion lo que se mueve es la actualizacion. Hereda su rotulo en
// versalitas y su tipografia, asi que al aparecer no cambia la forma de la
// pantalla, cambia el contenido de un bloque que ya estaba.
constexpr Rect ESTADO   = { MARGEN, VIVO_Y - 8, ANCHO, 76 };
constexpr int BARRA_H   = 8;
constexpr int BARRA_Y   = ESTADO.y + ESTADO.h - BARRA_H;
// Aire para el porcentaje alineado a la derecha, para que un texto largo no se
// le meta encima. En cursiva a 18 pt "100 %" mide 78.
constexpr int PCT_W     = 96;

bool dentro(const Rect& r, int16_t x, int16_t y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// Un dato: la etiqueta chica arriba en versalitas y el valor debajo, con el
// peso al reves de como estaba. Antes la etiqueta y el valor compartian renglon
// y tamano, asi que la pantalla no decia que era el dato y que era su nombre.
void dato(int x, int y, int w, const char* etiqueta, const char* valor) {
    g.setTextDatum(lgfx::top_left);
    g.setFont(DS::fontCaption());
    g.setTextColor(FG_4, CANVAS);
    g.drawString(etiqueta, x, y);
    g.setFont(DS::fontBody());
    g.setTextColor(FG, CANVAS);
    // Un SSID admite 32 caracteres y el host del feed puede traer puerto:
    // medidos, se iban del margen y terminaban a 13 px del borde del panel.
    uiTextoRecortado(g, valor, x, y + 12, w);
}

// LovyanGFX no tiene drawSmoothRoundRect: la unica variante suavizada es la que
// rellena. El borde de 1 px se arma con dos rellenos concentricos, el de afuera
// del color de la linea y el de adentro del canvas.
// El rotulo va en la tipografia de boton del design system, no en la del texto
// corrido: a 12 px quedaba flotando dentro de una pastilla de 44 px de alto.
void boton(const Rect& r, const char* texto, uint16_t color) {
    g.fillSmoothRoundRect(r.x, r.y, r.w, r.h, RADIO, LINE);
    g.fillSmoothRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, RADIO - 1, CANVAS);
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(color, CANVAS);
    g.drawString(texto, r.x + r.w / 2, r.y + r.h / 2, &SatoshiMedium18);
}

// "2 h 14 min", o "14 min" cuando no llega a la hora.
void formatearUptime(char* out, size_t n, uint32_t s) {
    const uint32_t h = s / 3600, m = (s % 3600) / 60;
    if (h > 0) snprintf(out, n, "%lu h %lu min", (unsigned long)h, (unsigned long)m);
    else       snprintf(out, n, "%lu min", (unsigned long)m);
}

}  // namespace

void uiConfigDraw(const ConfigInfo& info) {
    g.fillScreen(CANVAS);

    // Encabezado: el isotipo a color y el wordmark. Es, con el selector, la
    // unica pantalla donde el aparato habla de si mismo.
    uiMarkColor(g, MARGEN, MARK_Y);
    g.setFont(DS::fontDataLg());
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(FG, CANVAS);
    g.drawString("FERCED", MARGEN + UI_MARK_C_W + 18, MARK_Y + UI_MARK_C_H / 2);

    // Si el aparato tiene nombre, va acá: es lo mas alto de la jerarquia
    // despues de la marca, y un aparato con nombre tiene que decirlo donde se
    // mira primero. Sin nombre, el rotulo de siempre.
    char nombre[NVS_NOMBRE_LEN + 2];
    snprintf(nombre, sizeof(nombre), "%s",
             info.nombre && info.nombre[0] ? info.nombre : "CONFIGURACIÓN");
    if (info.nombre && info.nombre[0]) {
        for (char* c = nombre; *c; c++) *c = toupper((unsigned char)*c);
    }
    g.setFont(DS::fontCaption());
    g.setTextDatum(lgfx::middle_right);
    g.setTextColor(FG_4, CANVAS);
    g.drawString(nombre, SCREEN_W - MARGEN, MARK_Y + UI_MARK_C_H / 2);

    g.drawFastHLine(MARGEN, RULE_Y, ANCHO, LINE);

    int y = DATO_Y0;
    dato(COL1_X, y, COL_W, "VERSIÓN",   info.version);
    dato(COL2_X, y, COL_W, "SSID",      info.online ? info.ssid : "sin conexión");
    y += DATO_LH + 6;
    dato(COL1_X, y, COL_W, "COMMIT",    info.commit);
    dato(COL2_X, y, COL_W, "IP",        info.online ? info.ip : "-");
    y += DATO_LH + 6;
    dato(COL1_X, y, COL_W, "COMPILADO", info.built);
    dato(COL2_X, y, COL_W, "FEED",      info.endpoint);

    g.drawFastHLine(MARGEN, SEP_Y, ANCHO, LINE);

    // Los tres numeros que se mueven mientras mirás la pantalla, en la cursiva
    // con gracias de la marca. Es el unico bloque vivo de una pantalla que por
    // lo demas dice cosas fijas, y merece leerse distinto.
    const char* rotulos[3] = {"ENCENDIDO", "ANIMACIÓN", "ÍTEMS"};
    char valores[3][32];
    formatearUptime(valores[0], sizeof(valores[0]), info.uptimeS);
    snprintf(valores[1], sizeof(valores[1]), "%.1f fps", info.fps);
    snprintf(valores[2], sizeof(valores[2]), "%u", (unsigned)info.items);

    for (int i = 0; i < 3; i++) {
        g.setTextDatum(lgfx::top_left);
        g.setFont(DS::fontCaption());
        g.setTextColor(FG_4, CANVAS);
        g.drawString(rotulos[i], VIVO_X[i], VIVO_Y);
        g.setFont(DS::fontAcento());
        g.setTextColor(FG, CANVAS);
        uiTextoRecortado(g, valores[i], VIVO_X[i], VIVO_Y + 18, VIVO_W[i]);
    }

    boton(BTN_REFRESH, "Actualizar feed",       FG);
    boton(BTN_CLOSE,   "Cerrar",                FG);
    boton(BTN_UPDATE,  "Buscar actualización",  FG);
    boton(BTN_FORGET,  "Reaparear",             DANGER);

    // La franja de estado queda vacia: ya la pinto el fillScreen. Escribir algo
    // aca obligaria a uiConfigEstado a competir con el dibujo completo.
    uiAnimReveal();
}

// Repinta SOLO el rectangulo de la franja. Nada de pantalla entera: el callback
// de progreso entra una vez por punto porcentual y la pantalla completa cuesta
// 99 ms, o sea diez segundos de repintados por descarga.
void uiConfigEstado(const char* texto, int pct, bool error) {
    g.fillRect(ESTADO.x, ESTADO.y, ESTADO.w, ESTADO.h, CANVAS);

    if (pct > 100) pct = 100;

    // Rotulo en versalitas, el mismo recurso que titula todos los bloques de la
    // pantalla. Sin el, el mensaje era un renglon suelto que no se sabia de que
    // hablaba.
    g.setTextDatum(lgfx::top_left);
    g.setFont(DS::fontCaption());
    g.setTextColor(error ? DANGER : FG_4, CANVAS);
    g.drawString(error ? "NO SE PUDO" : "ACTUALIZACIÓN", ESTADO.x, VIVO_Y);

    // El mensaje toma el lugar y el tamano de los numeros vivos. Un error suele
    // ser largo —"se corto al 45%"— asi que va en la tipografia de titulo, que
    // entra; el estado normal es corto y luce mejor en la cursiva de la marca.
    const int anchoTexto = (pct < 0 ? ESTADO.w : ESTADO.w - PCT_W);
    g.setFont(error ? DS::fontHeading() : DS::fontAcento());
    g.setTextColor(error ? FG_2 : FG, CANVAS);
    uiTextoRecortado(g, texto, ESTADO.x, VIVO_Y + 18, anchoTexto);

    if (pct >= 0) {
        char pctTxt[8];
        snprintf(pctTxt, sizeof(pctTxt), "%d %%", pct);
        g.setFont(DS::fontAcento());
        g.setTextDatum(lgfx::top_right);
        g.setTextColor(FG, CANVAS);
        g.drawString(pctTxt, ESTADO.x + ESTADO.w, VIVO_Y + 18);

        // Riel completo y encima el tramo hecho, con el espectro de la marca: es
        // la misma barra que marca el paso del tiempo en las demas pantallas,
        // midiendo acá lo que baja el OTA. El minimo es el alto de la barra:
        // mas angosto que su propio diametro, fillSmoothRoundRect no dibuja nada
        // y el arranque de la descarga pareceria que no pasa.
        const int radio = BARRA_H / 2;
        g.fillSmoothRoundRect(ESTADO.x, BARRA_Y, ESTADO.w, BARRA_H, radio, LINE);
        int largo = ESTADO.w * pct / 100;
        if (largo < BARRA_H) largo = BARRA_H;
        for (int i = 0; i < largo; i++) {
            const float t = largo > 1 ? (float)i / (float)(largo - 1) : 1.0f;
            g.drawFastVLine(ESTADO.x + i, BARRA_Y, BARRA_H, fercedSpectrum(t));
        }
        uiRoundCorners(g, ESTADO.x, BARRA_Y, largo, BARRA_H, radio, CANVAS);
    }

    uiAnimPresent(ESTADO.y, ESTADO.h);
}

ConfigAction uiConfigHit(int16_t x, int16_t y) {
    if (dentro(BTN_REFRESH, x, y)) return CFG_REFRESH;
    if (dentro(BTN_UPDATE,  x, y)) return CFG_UPDATE;
    if (dentro(BTN_CLOSE,   x, y)) return CFG_CLOSE;
    if (dentro(BTN_FORGET,  x, y)) return CFG_FORGET;
    return CFG_NONE;
}

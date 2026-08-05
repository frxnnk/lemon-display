#include "ui_config.h"

#include "display_manager.h"
#include "design_system.h"
#include "ui_ferced.h"          // FercedColors
#include "data/ferced_mark_11.h"
#include "config.h"

#include <cstddef>
#include <cstdio>

using namespace FercedColors;

namespace {

struct Rect { int16_t x, y, w, h; };

constexpr int MARGEN    = 32;
constexpr int ANCHO     = SCREEN_W - 2 * MARGEN;   // 416
// La etiqueta mas larga ("Compilado") termina en x=116, medido en el simulador.
// Con la columna en 200 quedaba un lago de 84 px contra etiquetas cortas como
// "IP"; 150 deja 34 px de canaleta y de sobra para el valor mas largo.
constexpr int VALOR_X   = 150;
// Satoshi12 avanza 24 px por linea, asi que la fila ocupa FILA_H completos.
constexpr int FILA_H    = 25;
// El cuarto boton no pide alto: entra en la segunda columna de una fila que ya
// existia. Quien lo pide es la franja de estado, 36 px que antes no estaban, y
// salen del aire y no de los datos: 10 px de la sangria bajo el wordmark, 6 de
// cada separador —que siguen centrados entre bloque y bloque—, 6 de cada fila
// de botones y 4 del margen de abajo. Ningun dato se fue de la pantalla.
constexpr int FILA_Y0   = 60;
constexpr int SEP_GAP   = 16;                      // aire extra tras cada bloque
constexpr int RADIO     = DS::RADIUS_MD;           // 12
// BTN_H_LG (50) ya no entra dos veces junto con la franja de estado. BTN_H_MD
// son 44, que sigue siendo el minimo tactil que fija el design system.
constexpr int BTN_H     = DS::BTN_H_MD;            // 44

// El mark vive en 11x28 y se dibuja a escala entera para no interpolar.
constexpr int MARK_W = 11;
constexpr int MARK_H = 28;

// Cuatro botones en dos filas de dos, en vez de la fila de dos + la barra de
// ancho completo que habia: una tercera fila no entra en 480 px de alto.
// Las columnas parten los 416 px en 248 + 152 con 16 de aire y agrupan por
// consecuencia: a la izquierda lo que sale a la red, a la derecha lo que corta.
//
// El corte esta medido, no elegido a ojo. Con SatoshiMedium18 los rotulos
// miden 162 ("Actualizar feed"), 217 ("Buscar actualización"), 69 ("Cerrar")
// y 110 ("Reaparear"): la columna ancha les deja 15 px de aire al mas largo y
// la angosta 21. "Reaparear WiFi" media 163 y se desbordaba de la pastilla
// angosta; el rotulo perdio el sustantivo, no el verbo, que es lo que nombra
// la accion. Que ademas pase de 416 px de ancho a 152 en la esquina le quita
// superficie donde rozarlo, que era el motivo de tenerlo separado.
constexpr Rect BTN_REFRESH = { MARGEN, 368, 248, BTN_H };
constexpr Rect BTN_CLOSE   = { 296,    368, 152, BTN_H };
constexpr Rect BTN_UPDATE  = { MARGEN, 422, 248, BTN_H };
constexpr Rect BTN_FORGET  = { 296,    422, 152, BTN_H };

// La franja de estado. Vive entre los datos y los botones y en reposo esta
// vacia; durante una actualizacion es lo UNICO que se repinta. El renglon de
// Satoshi12 ocupa los primeros 24 px y la barra los ultimos 8.
// Va pegada al bloque de datos (6 px) y separada de los botones (10 px) a
// proposito: lo que dice es informacion, no una accion mas.
constexpr Rect ESTADO   = { MARGEN, 322, ANCHO, 36 };
constexpr int BARRA_H   = 8;
constexpr int BARRA_Y   = ESTADO.y + ESTADO.h - BARRA_H;
// Aire para el porcentaje alineado a la derecha, para que un texto largo no se
// le meta encima.
constexpr int PCT_W     = 60;

bool dentro(const Rect& r, int16_t x, int16_t y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void marca(int x, int y, int escala) {
    for (int py = 0; py < MARK_H; py++) {
        for (int px = 0; px < MARK_W; px++) {
            const uint16_t c = pgm_read_word(&ferced_mark_11[py * MARK_W + px]);
            if (c == 0x0000) continue;
            tft.fillRect(x + px * escala, y + py * escala, escala, escala, c);
        }
    }
}

// Ancho util de la columna de valores hasta el margen derecho.
constexpr int VALOR_W = SCREEN_W - MARGEN - VALOR_X;   // 298

// Un SSID admite 32 caracteres y el host del feed puede traer puerto: medidos
// en el simulador se iban del margen y terminaban a 13 px del borde del panel.
// Se recortan con puntos suspensivos, midiendo el candidato ya con los puntos
// para que el resultado nunca se pase. Los motivos de error del OTA entran por
// la misma funcion, que por eso toma el ancho en vez de suponer la columna.
void dibujarRecortado(int x, int y, int ancho, const char* texto) {
    if (tft.textWidth(texto, &Satoshi12) <= ancho) {
        tft.drawString(texto, x, y, &Satoshi12);
        return;
    }
    char buf[80];
    size_t cabe = 0;
    for (size_t n = 1; texto[n] != '\0' && n < sizeof(buf) - 4; n++) {
        snprintf(buf, sizeof(buf), "%.*s...", (int)n, texto);
        if (tft.textWidth(buf, &Satoshi12) > ancho) break;
        cabe = n;
    }
    snprintf(buf, sizeof(buf), "%.*s...", (int)cabe, texto);
    tft.drawString(buf, x, y, &Satoshi12);
}

void fila(int y, const char* etiqueta, const char* valor) {
    tft.setTextDatum(lgfx::top_left);
    tft.setTextColor(FG_3, CANVAS);
    tft.drawString(etiqueta, MARGEN, y, &Satoshi12);
    tft.setTextColor(FG, CANVAS);
    dibujarRecortado(VALOR_X, y, VALOR_W, valor);
}

// Recibe la y donde quedo el cursor tras un bloque y centra la linea en el
// aire que sigue: derivarla de SEP_GAP evita que un cambio de espaciado la
// deje pegada al bloque de abajo, como paso al comprimir la pantalla.
void separador(int y) {
    tft.drawFastHLine(MARGEN, y + SEP_GAP / 2 - 1, ANCHO, LINE);
}

// LovyanGFX no tiene drawSmoothRoundRect: la unica variante suavizada es la que
// rellena. El borde de 1 px se arma con dos rellenos concentricos, el de afuera
// del color de la linea y el de adentro del canvas, que es como lo resuelve
// provisionDrawQR() para la tarjeta del QR.
// El rotulo va en la tipografia de boton del design system, no en la del texto
// corrido: a 12 px quedaba flotando dentro de una pastilla de 50 px de alto.
void boton(const Rect& r, const char* texto, uint16_t color) {
    tft.fillSmoothRoundRect(r.x, r.y, r.w, r.h, RADIO, LINE);
    tft.fillSmoothRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, RADIO - 1, CANVAS);
    tft.setTextDatum(lgfx::middle_center);
    tft.setTextColor(color, CANVAS);
    tft.drawString(texto, r.x + r.w / 2, r.y + r.h / 2, &SatoshiMedium18);
}

// "2 h 14 min", o "14 min" cuando no llega a la hora.
void formatearUptime(char* out, size_t n, uint32_t s) {
    const uint32_t h = s / 3600, m = (s % 3600) / 60;
    if (h > 0) snprintf(out, n, "%lu h %lu min", (unsigned long)h, (unsigned long)m);
    else       snprintf(out, n, "%lu min", (unsigned long)m);
}

}  // namespace

void uiConfigDraw(const ConfigInfo& info) {
    // Esta pantalla dibuja directo sobre tft, asi que el sprite de la
    // animacion queda con contenido que ya no esta en el panel. Declararlo
    // aca y no en quien llama hace imposible olvidarselo.
    uiAnimInvalidate();
    displayWaitVSync();
    tft.fillScreen(CANVAS);

    // Encabezado: mark a escala 1 con el wordmark al lado.
    marca(MARGEN, 20, 1);
    tft.setTextDatum(lgfx::middle_left);
    tft.setTextColor(FG, CANVAS);
    tft.drawString("FERCED", MARGEN + MARK_W + 12, 20 + MARK_H / 2, &SatoshiMedium18);

    int y = FILA_Y0;
    // Los rotulos van acentuados: las fuentes se regeneraron hasta 0xFF, asi
    // que la pantalla puede escribir castellano de verdad.
    fila(y, "Versión",   info.version);      y += FILA_H;
    fila(y, "Commit",    info.commit);       y += FILA_H;
    fila(y, "Compilado", info.built);        y += FILA_H;

    // La linea va centrada en el aire que sigue, no pegada al bloque anterior:
    // asi separa en vez de parecer que subraya. Lo resuelve separador().
    separador(y); y += SEP_GAP;

    fila(y, "Red",  info.online ? info.ssid : "sin conexión"); y += FILA_H;
    fila(y, "IP",   info.online ? info.ip : "-");              y += FILA_H;
    fila(y, "Feed", info.endpoint);                            y += FILA_H;

    separador(y); y += SEP_GAP;

    char buf[32];
    formatearUptime(buf, sizeof(buf), info.uptimeS);
    fila(y, "Encendido", buf); y += FILA_H;
    snprintf(buf, sizeof(buf), "%.1f fps", info.fps);
    fila(y, "Animación", buf); y += FILA_H;
    snprintf(buf, sizeof(buf), "%u", (unsigned)info.items);
    fila(y, "Ítems", buf);

    boton(BTN_REFRESH, "Actualizar feed",       FG);
    boton(BTN_CLOSE,   "Cerrar",                FG);
    boton(BTN_UPDATE,  "Buscar actualización",  FG);
    boton(BTN_FORGET,  "Reaparear",             DANGER);

    // La franja de estado queda vacia: ya la pinto el fillScreen. Escribir algo
    // aca obligaria a uiConfigEstado a competir con el dibujo completo.
}

// Repinta SOLO el rectangulo de la franja. Nada de fillScreen: el callback de
// progreso entra una vez por punto porcentual y la pantalla entera cuesta
// 99 ms, o sea diez segundos de repintados por descarga.
void uiConfigEstado(const char* texto, int pct, bool error) {
    displayWaitVSync();
    tft.fillRect(ESTADO.x, ESTADO.y, ESTADO.w, ESTADO.h, CANVAS);

    if (pct > 100) pct = 100;

    tft.setTextDatum(lgfx::top_left);
    tft.setTextColor(error ? DANGER : FG, CANVAS);
    dibujarRecortado(ESTADO.x, ESTADO.y,
                     pct < 0 ? ESTADO.w : ESTADO.w - PCT_W, texto);

    if (pct < 0) return;

    char pctTxt[8];
    snprintf(pctTxt, sizeof(pctTxt), "%d %%", pct);
    tft.setTextDatum(lgfx::top_right);
    tft.setTextColor(FG_3, CANVAS);
    tft.drawString(pctTxt, ESTADO.x + ESTADO.w, ESTADO.y, &Satoshi12);

    // Riel completo y encima el tramo hecho. El minimo es el alto de la barra:
    // mas angosto que su propio diametro, fillSmoothRoundRect no dibuja nada y
    // el arranque de la descarga pareceria que no pasa.
    const int radio = BARRA_H / 2;
    tft.fillSmoothRoundRect(ESTADO.x, BARRA_Y, ESTADO.w, BARRA_H, radio, LINE);
    int largo = ESTADO.w * pct / 100;
    if (largo < BARRA_H) largo = BARRA_H;
    tft.fillSmoothRoundRect(ESTADO.x, BARRA_Y, largo, BARRA_H, radio, FG);
}

ConfigAction uiConfigHit(int16_t x, int16_t y) {
    if (dentro(BTN_REFRESH, x, y)) return CFG_REFRESH;
    if (dentro(BTN_UPDATE,  x, y)) return CFG_UPDATE;
    if (dentro(BTN_CLOSE,   x, y)) return CFG_CLOSE;
    if (dentro(BTN_FORGET,  x, y)) return CFG_FORGET;
    return CFG_NONE;
}

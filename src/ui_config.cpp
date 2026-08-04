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
constexpr int FILA_Y0   = 70;
constexpr int SEP_GAP   = 22;                      // aire extra tras cada bloque
constexpr int RADIO     = DS::RADIUS_MD;           // 12
constexpr int BTN_H     = DS::BTN_H_LG;            // 50

// El mark vive en 11x28 y se dibuja a escala entera para no interpolar.
constexpr int MARK_W = 11;
constexpr int MARK_H = 28;

// La fila de arriba parte los 416 px en 244 + 156 con 16 de aire: "Actualizar
// feed" es el texto largo y se lleva la parte grande. La de abajo arranca 12 px
// mas abajo y termina en 462, a 18 px del borde.
constexpr Rect BTN_REFRESH = { MARGEN, 350, 244, BTN_H };
constexpr Rect BTN_CLOSE   = { 292,    350, 156, BTN_H };
constexpr Rect BTN_FORGET  = { MARGEN, 412, ANCHO, BTN_H };

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
// para que el resultado nunca se pase.
void dibujarValor(int y, const char* valor) {
    if (tft.textWidth(valor, &Satoshi12) <= VALOR_W) {
        tft.drawString(valor, VALOR_X, y, &Satoshi12);
        return;
    }
    char buf[80];
    size_t cabe = 0;
    for (size_t n = 1; valor[n] != '\0' && n < sizeof(buf) - 4; n++) {
        snprintf(buf, sizeof(buf), "%.*s...", (int)n, valor);
        if (tft.textWidth(buf, &Satoshi12) > VALOR_W) break;
        cabe = n;
    }
    snprintf(buf, sizeof(buf), "%.*s...", (int)cabe, valor);
    tft.drawString(buf, VALOR_X, y, &Satoshi12);
}

void fila(int y, const char* etiqueta, const char* valor) {
    tft.setTextDatum(lgfx::top_left);
    tft.setTextColor(FG_3, CANVAS);
    tft.drawString(etiqueta, MARGEN, y, &Satoshi12);
    tft.setTextColor(FG, CANVAS);
    dibujarValor(y, valor);
}

void separador(int y) {
    tft.drawFastHLine(MARGEN, y, ANCHO, LINE);
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

    // +10 centra la linea entre el renglon de arriba y el de abajo: pegada al
    // bloque anterior parecia subrayarlo en vez de separar.
    separador(y + 10); y += SEP_GAP;

    fila(y, "Red",  info.online ? info.ssid : "sin conexión"); y += FILA_H;
    fila(y, "IP",   info.online ? info.ip : "-");              y += FILA_H;
    fila(y, "Feed", info.endpoint);                            y += FILA_H;

    separador(y + 10); y += SEP_GAP;

    char buf[32];
    formatearUptime(buf, sizeof(buf), info.uptimeS);
    fila(y, "Encendido", buf); y += FILA_H;
    snprintf(buf, sizeof(buf), "%.1f fps", info.fps);
    fila(y, "Animación", buf); y += FILA_H;
    snprintf(buf, sizeof(buf), "%u", (unsigned)info.items);
    fila(y, "Ítems", buf);

    boton(BTN_REFRESH, "Actualizar feed", FG);
    boton(BTN_CLOSE,   "Cerrar",          FG);
    boton(BTN_FORGET,  "Reaparear WiFi",  DANGER);
}

ConfigAction uiConfigHit(int16_t x, int16_t y) {
    if (dentro(BTN_REFRESH, x, y)) return CFG_REFRESH;
    if (dentro(BTN_CLOSE,   x, y)) return CFG_CLOSE;
    if (dentro(BTN_FORGET,  x, y)) return CFG_FORGET;
    return CFG_NONE;
}

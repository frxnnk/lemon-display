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
constexpr int VALOR_X   = 200;
constexpr int FILA_H    = 26;
constexpr int RADIO     = DS::RADIUS_MD;           // 12

// El mark vive en 11x28 y se dibuja a escala entera para no interpolar.
constexpr int MARK_W = 11;
constexpr int MARK_H = 28;

constexpr Rect BTN_REFRESH = { MARGEN, 350, 250, 50 };
constexpr Rect BTN_CLOSE   = { 300,    350, 148, 50 };
constexpr Rect BTN_FORGET  = { MARGEN, 410, ANCHO, 50 };

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

void fila(int y, const char* etiqueta, const char* valor) {
    tft.setTextDatum(lgfx::top_left);
    tft.setTextColor(FG_3, CANVAS);
    tft.drawString(etiqueta, MARGEN, y, &Satoshi12);
    tft.setTextColor(FG, CANVAS);
    tft.drawString(valor, VALOR_X, y, &Satoshi12);
}

void separador(int y) {
    tft.drawFastHLine(MARGEN, y, ANCHO, LINE);
}

// LovyanGFX no tiene drawSmoothRoundRect: la unica variante suavizada es la que
// rellena. El borde de 1 px se arma con dos rellenos concentricos, el de afuera
// del color de la linea y el de adentro del canvas, que es como lo resuelve
// provisionDrawQR() para la tarjeta del QR.
void boton(const Rect& r, const char* texto, uint16_t color) {
    tft.fillSmoothRoundRect(r.x, r.y, r.w, r.h, RADIO, LINE);
    tft.fillSmoothRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, RADIO - 1, CANVAS);
    tft.setTextDatum(lgfx::middle_center);
    tft.setTextColor(color, CANVAS);
    tft.drawString(texto, r.x + r.w / 2, r.y + r.h / 2, &Satoshi12);
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

    int y = 80;
    fila(y, "Version",   info.version);      y += FILA_H;
    fila(y, "Commit",    info.commit);       y += FILA_H;
    fila(y, "Compilado", info.built);        y += FILA_H;

    separador(y + 4); y += 24;

    fila(y, "Red",  info.online ? info.ssid : "sin conexion"); y += FILA_H;
    fila(y, "IP",   info.online ? info.ip : "-");              y += FILA_H;
    fila(y, "Feed", info.endpoint);                            y += FILA_H;

    separador(y + 4); y += 24;

    char buf[32];
    formatearUptime(buf, sizeof(buf), info.uptimeS);
    fila(y, "Encendido", buf); y += FILA_H;
    snprintf(buf, sizeof(buf), "%.1f fps", info.fps);
    fila(y, "Animacion", buf); y += FILA_H;
    snprintf(buf, sizeof(buf), "%u", (unsigned)info.items);
    fila(y, "Items", buf);

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

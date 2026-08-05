#include "ui_launcher.h"
#include "config.h"
#include "design_system.h"
#include "data/ferced_mark_11.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;

// El selector es la única pantalla del aparato que muestra la marca completa y
// no contenido. Acá el aparato habla de sí mismo, así que se permite el
// wordmark y el encabezado.
//
// Cada app es una tarjeta con monograma. La que está corriendo se dibuja en
// negativo —monograma relleno, texto en blanco— y las demás en contorno. El
// contraste hace de indicador sin gastar un color de acento, que en esta
// identidad está reservado para estados.
//
// Se dibuja directo sobre tft y no sobre el sprite: es una pantalla estática,
// como la de configuración, y así no compite con el buffer de la animación.

namespace {

constexpr int MARGIN = 28;
constexpr int MARK_W = 11;
constexpr int MARK_H = 28;
constexpr int HEAD_Y = 38;
constexpr int RULE_Y = 100;

constexpr int CARD_X = MARGIN;
constexpr int CARD_W = SCREEN_W - 2 * MARGIN;   // 424
constexpr int CARD_Y0 = 140;
constexpr int CARD_H = 104;
constexpr int CARD_GAP = 20;
constexpr int CARD_R = DS::RADIUS_LG;           // 16

constexpr int MONO = 54;                        // lado del monograma
constexpr int MONO_R = 14;
constexpr int TEXT_X = CARD_X + 20 + MONO + 20;

constexpr int DOTS_Y = 420;
constexpr int HINT_Y = 442;

constexpr uint8_t MAX_APPS = 4;

uint8_t s_n = 0;
uint8_t s_actual = 0;
bool    s_ready = false;

int cardY(uint8_t i) { return CARD_Y0 + i * (CARD_H + CARD_GAP); }

void marca(int x, int y) {
    for (int py = 0; py < MARK_H; py++) {
        for (int px = 0; px < MARK_W; px++) {
            const uint16_t c = pgm_read_word(&ferced_mark_11[py * MARK_W + px]);
            if (c == 0x0000) continue;
            tft.drawPixel(x + px, y + py, c);
        }
    }
}

// LovyanGFX no tiene drawSmoothRoundRect: el borde de 1 px se arma con dos
// rellenos concéntricos, como ya lo resuelven la tarjeta del QR y los botones
// de la pantalla de configuración.
void marco(int x, int y, int w, int h, int r, uint16_t color) {
    tft.fillSmoothRoundRect(x, y, w, h, r, color);
    tft.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, CANVAS);
}

void tarjeta(uint8_t i, const AppInfo& app) {
    const int y = cardY(i);
    const bool activa = i == s_actual;

    marco(CARD_X, y, CARD_W, CARD_H, CARD_R, activa ? FG_2 : LINE);

    const int mx = CARD_X + 20;
    const int my = y + (CARD_H - MONO) / 2;
    const char inicial[2] = {app.inicial, '\0'};
    if (activa) {
        tft.fillSmoothRoundRect(mx, my, MONO, MONO, MONO_R, FG);
        tft.setTextColor(CANVAS, FG);
    } else {
        marco(mx, my, MONO, MONO, MONO_R, LINE);
        tft.setTextColor(FG_3, CANVAS);
    }
    tft.setFont(DS::fontHeading());
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString(inicial, mx + MONO / 2, my + MONO / 2);

    tft.setTextDatum(lgfx::top_left);
    tft.setFont(DS::fontHeading());
    tft.setTextColor(activa ? FG : FG_2, CANVAS);
    tft.drawString(app.nombre, TEXT_X, y + 30);

    if (app.estado[0]) {
        tft.setFont(DS::fontBody());
        tft.setTextColor(FG_3, CANVAS);
        tft.drawString(app.estado, TEXT_X, y + 60);
    }
}

}  // namespace

void uiLauncherSetup() { s_ready = true; }

void uiLauncherDraw(const AppInfo* apps, uint8_t n, uint8_t actual) {
    if (!s_ready || !apps) return;
    if (n > MAX_APPS) n = MAX_APPS;
    s_n = n;
    s_actual = actual < n ? actual : 0;

    displayWaitVSync();
    tft.fillScreen(CANVAS);

    marca(MARGIN, HEAD_Y);
    tft.setFont(DS::fontHeading());
    tft.setTextDatum(lgfx::middle_left);
    tft.setTextColor(FG, CANVAS);
    tft.drawString("FERCED", MARGIN + MARK_W + 12, HEAD_Y + MARK_H / 2);

    tft.setFont(DS::fontCaption());
    tft.setTextDatum(lgfx::middle_right);
    tft.setTextColor(FG_4, CANVAS);
    tft.drawString("APLICACIONES", SCREEN_W - MARGIN, HEAD_Y + MARK_H / 2);

    tft.drawFastHLine(MARGIN, RULE_Y, SCREEN_W - 2 * MARGIN, LINE);

    for (uint8_t i = 0; i < n; i++) tarjeta(i, apps[i]);

    // Puntos de posición: los mismos que marcan en qué app se está cuando se
    // cambia con un deslizamiento, sin abrir el selector.
    const int paso = 20;
    const int x0 = SCREEN_W / 2 - (n - 1) * paso / 2;
    for (uint8_t i = 0; i < n; i++) {
        tft.fillCircle(x0 + i * paso, DOTS_Y, i == s_actual ? 4 : 3,
                       i == s_actual ? FG : FG_4);
    }

    tft.setFont(DS::fontCaption());
    tft.setTextDatum(lgfx::top_center);
    tft.setTextColor(FG_4, CANVAS);
    tft.drawString("tocá una app  ·  deslizá para cambiar", SCREEN_W / 2, HINT_Y);
}

int8_t uiLauncherHit(int16_t x, int16_t y) {
    if (x < CARD_X || x >= CARD_X + CARD_W) return -1;
    for (uint8_t i = 0; i < s_n; i++) {
        const int cy = cardY(i);
        if (y >= cy && y < cy + CARD_H) return (int8_t)i;
    }
    return -1;
}

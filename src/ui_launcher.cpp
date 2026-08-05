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

// La grilla se calcula a partir de cuántas apps hay, no está fijada para dos.
// Con la altura clavada en 104, la tercera tarjeta se salía de la pantalla y se
// comía los puntos y el pie: 140 + 2*(104+20) + 104 = 492 sobre un panel de 480.
constexpr int CARD_Y0  = 132;
constexpr int CARD_FIN = 404;                   // desde acá abajo van puntos y pie
constexpr int CARD_GAP = 18;

constexpr int MONO_MAX = 54;                    // lado del monograma
constexpr int TEXT_X = CARD_X + 20 + MONO_MAX + 20;

constexpr int DOTS_Y = 424;
constexpr int HINT_Y = 446;

constexpr uint8_t MAX_APPS = 4;

uint8_t s_n = 0;
uint8_t s_actual = 0;
bool    s_ready = false;

int cardH() {
    if (s_n == 0) return 0;
    const int alto = (CARD_FIN - CARD_Y0 - (s_n - 1) * CARD_GAP) / s_n;
    return alto > 126 ? 126 : alto;   // con una sola app, una tarjeta gigante queda ridícula
}

int cardY(uint8_t i) { return CARD_Y0 + i * (cardH() + CARD_GAP); }

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
    const int h = cardH();
    const bool activa = i == s_actual;
    const int radio = h / 4 < DS::RADIUS_LG ? h / 4 : DS::RADIUS_LG;

    marco(CARD_X, y, CARD_W, h, radio, activa ? FG_2 : LINE);

    // El monograma se achica si la tarjeta no le da lugar, en vez de desbordar.
    int mono = h - 24;
    if (mono > MONO_MAX) mono = MONO_MAX;
    const int mx = CARD_X + 20 + (MONO_MAX - mono) / 2;   // la columna de texto no se mueve
    const int my = y + (h - mono) / 2;
    const char inicial[2] = {app.inicial, '\0'};
    if (activa) {
        tft.fillSmoothRoundRect(mx, my, mono, mono, mono / 4, FG);
        tft.setTextColor(CANVAS, FG);
    } else {
        marco(mx, my, mono, mono, mono / 4, LINE);
        tft.setTextColor(FG_3, CANVAS);
    }
    tft.setFont(DS::fontHeading());
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString(inicial, mx + mono / 2, my + mono / 2);

    // Nombre y estado centrados como bloque, para que la tarjeta se vea igual
    // de equilibrada con dos apps que con cuatro.
    constexpr int BLOQUE = 48;
    const int ty = y + (h - BLOQUE) / 2;

    tft.setTextDatum(lgfx::top_left);
    tft.setFont(DS::fontHeading());
    tft.setTextColor(activa ? FG : FG_2, CANVAS);
    tft.drawString(app.nombre, TEXT_X, ty);

    if (app.estado[0]) {
        tft.setFont(DS::fontBody());
        tft.setTextColor(FG_3, CANVAS);
        tft.drawString(app.estado, TEXT_X, ty + 30);
    }
}

}  // namespace

void uiLauncherSetup() { s_ready = true; }

void uiLauncherDraw(const AppInfo* apps, uint8_t n, uint8_t actual) {
    if (!s_ready || !apps) return;
    if (n > MAX_APPS) n = MAX_APPS;
    s_n = n;
    s_actual = actual < n ? actual : 0;

    // Esta pantalla dibuja directo sobre tft, asi que el sprite de la
    // animacion queda con contenido que ya no esta en el panel. Declararlo
    // aca y no en quien llama hace imposible olvidarselo.
    uiAnimInvalidate();
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
    const int h = cardH();
    for (uint8_t i = 0; i < s_n; i++) {
        const int cy = cardY(i);
        // La mitad del hueco entre tarjetas cuenta para la de arriba: un dedo
        // que cae justo en el borde tiene que hacer algo, no nada.
        if (y >= cy && y < cy + h + CARD_GAP / 2) return (int8_t)i;
    }
    return -1;
}

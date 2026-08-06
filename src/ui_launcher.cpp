#include "ui_launcher.h"
#include "config.h"
#include "design_system.h"
#include "ui_chrome.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;
using namespace FercedChrome;

// El selector es la única pantalla del aparato —junto con configuración— que
// muestra la marca completa y no contenido. Acá el aparato habla de sí mismo,
// así que se permite el isotipo a color y el wordmark. En las pantallas de
// contenido la marca vuelve a ser la de 11x28 en blanco, arriba a la derecha:
// ahí el protagonista es el titular.
//
// Cada app es una baldosa con monograma. La que está corriendo se dibuja en
// negativo —monograma relleno, texto en blanco— y las demás en contorno. El
// contraste hace de indicador sin gastar un color de acento, que en esta
// identidad está reservado para estados.
//
// La grilla es de dos columnas. Antes era una lista de una columna con la
// altura repartida entre las apps, y con la cuarta las tarjetas bajaban a 60 px
// y el bloque de texto se cruzaba con el borde. Con dos columnas entran seis
// sin apretar nada.

namespace {

LGFX_Sprite& g = uiSprite;

constexpr int RULE = 112;

constexpr int GRID_Y0  = 130;
constexpr int GRID_FIN = 404;
constexpr int GAP      = 20;

constexpr int HINT_Y = 422;

uint8_t s_n = 0;
uint8_t s_actual = 0;
bool    s_ready = false;

uint8_t columnas() { return s_n > 1 ? 2 : 1; }
uint8_t renglones() { return (uint8_t)((s_n + columnas() - 1) / columnas()); }

int cardW() {
    const uint8_t c = columnas();
    return (SCREEN_W - 2 * MARGIN - (c - 1) * GAP) / c;
}

int cardH() {
    if (s_n == 0) return 0;
    const uint8_t r = renglones();
    const int alto = (GRID_FIN - GRID_Y0 - (r - 1) * GAP) / r;
    return alto > 150 ? 150 : alto;   // con una sola app, una baldosa gigante queda ridícula
}

int cardX(uint8_t i) { return MARGIN + (i % columnas()) * (cardW() + GAP); }
int cardY(uint8_t i) { return GRID_Y0 + (i / columnas()) * (cardH() + GAP); }

// LovyanGFX no tiene drawSmoothRoundRect: el borde de 1 px se arma con dos
// rellenos concéntricos, como ya lo resuelven la tarjeta del QR y los botones
// de la pantalla de configuración.
void marco(int x, int y, int w, int h, int r, uint16_t color, uint16_t dentro) {
    g.fillSmoothRoundRect(x, y, w, h, r, color);
    g.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, dentro);
}

void monograma(int x, int y, int lado, char inicial, bool activa) {
    if (activa) {
        g.fillSmoothRoundRect(x, y, lado, lado, lado / 4, FG);
        g.setTextColor(CANVAS, FG);
    } else {
        marco(x, y, lado, lado, lado / 4, LINE, CANVAS);
        g.setTextColor(FG_3, CANVAS);
    }
    const char txt[2] = {inicial, '\0'};
    g.setFont(DS::fontHeading());
    g.setTextDatum(lgfx::middle_center);
    g.drawString(txt, x + lado / 2, y + lado / 2);
}

void baldosa(uint8_t i, const AppInfo& app) {
    const int x = cardX(i), y = cardY(i);
    const int w = cardW(), h = cardH();
    const bool activa = i == s_actual;
    const int radio = h / 5 < DS::RADIUS_LG ? h / 5 : DS::RADIUS_LG;

    marco(x, y, w, h, radio, activa ? FG_2 : LINE, activa ? SURFACE : CANVAS);
    const uint16_t fondo = activa ? SURFACE : CANVAS;

    // Con seis apps la baldosa baja de 100 px y el monograma arriba ya no entra
    // con el texto debajo: ahí se pasa a la disposición horizontal, que es la
    // que tenía la lista de una columna.
    const bool vertical = h >= 100;
    const int lado = vertical ? 40 : (h - 28 > 40 ? 40 : h - 28);
    int tx, ty;

    if (vertical) {
        monograma(x + 18, y + 18, lado, app.inicial, activa);
        tx = x + 18;
        ty = y + h - 56;
    } else {
        monograma(x + 16, y + (h - lado) / 2, lado, app.inicial, activa);
        tx = x + 16 + lado + 14;
        ty = y + h / 2 - 22;
    }

    const int maxW = x + w - 14 - tx;
    g.setTextDatum(lgfx::top_left);
    g.setFont(DS::fontHeading());
    g.setTextColor(activa ? FG : FG_2, fondo);
    uiTextoRecortado(g, app.nombre, tx, ty, maxW);

    if (app.estado[0]) {
        g.setFont(DS::fontCaption());
        g.setTextColor(FG_3, fondo);
        uiTextoRecortado(g, app.estado, tx, ty + 28, maxW);
    }
}

}  // namespace

void uiLauncherSetup() { uiAnimSetup(); s_ready = true; }

void uiLauncherDraw(const AppInfo* apps, uint8_t n, uint8_t actual) {
    if (!s_ready || !apps) return;
    if (n > UI_LAUNCHER_MAX_APPS) n = UI_LAUNCHER_MAX_APPS;
    s_n = n;
    s_actual = actual < n ? actual : 0;

    g.fillScreen(CANVAS);

    uiMarkColor(g, MARGIN, 28);
    g.setFont(DS::fontDataLg());
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(FG, CANVAS);
    g.drawString("FERCED", MARGIN + UI_MARK_C_W + 18, 28 + UI_MARK_C_H / 2);

    g.setFont(DS::fontCaption());
    g.setTextDatum(lgfx::middle_right);
    g.setTextColor(FG_4, CANVAS);
    g.drawString("APLICACIONES", SCREEN_W - MARGIN, 28 + UI_MARK_C_H / 2);

    g.drawFastHLine(MARGIN, RULE, SCREEN_W - 2 * MARGIN, LINE);

    for (uint8_t i = 0; i < n; i++) baldosa(i, apps[i]);

    g.setFont(DS::fontCaption());
    g.setTextDatum(lgfx::top_center);
    g.setTextColor(FG_4, CANVAS);
    g.drawString("tocá una app  ·  deslizá para cambiar", SCREEN_W / 2, HINT_Y);

    // Acá el riel no mide nada: es la firma. Es la única pantalla, con la de
    // configuración, donde el espectro entero tiene sentido, porque es donde el
    // aparato dice de quién es.
    uiRail(g, RAIL_Y, 1.0f);
    uiAnimReveal();
}

int8_t uiLauncherHit(int16_t x, int16_t y) {
    const int w = cardW(), h = cardH();
    for (uint8_t i = 0; i < s_n; i++) {
        const int cx = cardX(i), cy = cardY(i);
        // La mitad del hueco entre baldosas cuenta para la de arriba y la de la
        // izquierda: un dedo que cae justo en el borde tiene que hacer algo.
        if (x >= cx && x < cx + w + GAP / 2 && y >= cy && y < cy + h + GAP / 2) {
            return (int8_t)i;
        }
    }
    return -1;
}

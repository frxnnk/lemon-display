#include "ui_notif.h"
#include "config.h"
#include "design_system.h"
#include "data/ferced_mark_11.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;

namespace {

constexpr int MARGIN = 28;
constexpr int MARK_W = 11;
constexpr int MARK_H = 28;
constexpr int CHIP_Y = 30;
constexpr int CHIP_H = 28;

// ── Tarjeta ──
constexpr int TIT_Y   = 132;   // el título, en dos renglones como mucho
constexpr int TIT_LH  = 40;
constexpr int CUE_Y   = 240;   // el cuerpo, en tres
constexpr int CUE_LH  = 28;
constexpr int CUANDO_Y = 396;
constexpr int BARRA_Y  = 452;
constexpr int BARRA_H  = 4;

// ── Lista ──
constexpr int FILA_Y0 = 96;
constexpr int FILA_H  = 60;
constexpr int MAX_FILAS = 6;   // 96 + 6*60 = 456
constexpr int PUNTO_X = MARGIN + 5;

uint8_t s_filas = 0;
bool    s_ready = false;

void marca(int x, int y) {
    for (int py = 0; py < MARK_H; py++) {
        for (int px = 0; px < MARK_W; px++) {
            const uint16_t c = pgm_read_word(&ferced_mark_11[py * MARK_W + px]);
            if (c == 0x0000) continue;
            tft.drawPixel(x + px, y + py, c);
        }
    }
}

void chip(const char* texto) {
    tft.setFont(DS::fontCaption());
    const int w = tft.textWidth(texto) + 26;
    tft.fillSmoothRoundRect(MARGIN, CHIP_Y, w, CHIP_H, CHIP_H / 2, SURFACE);
    tft.setTextDatum(lgfx::middle_left);
    tft.setTextColor(FG_3, SURFACE);
    tft.drawString(texto, MARGIN + 13, CHIP_Y + CHIP_H / 2);
    marca(SCREEN_W - MARGIN - MARK_W, CHIP_Y);
}

// Corta por ancho real en píxeles y devuelve cuántos renglones usó. Los avisos
// vienen de la salida de un agente: pueden ser una palabra o una parrafada.
int envolver(const char* texto, int x, int y, int lh, int maxW, int maxLineas,
             const lgfx::IFont* font) {
    if (!texto || !texto[0]) return 0;

    tft.setFont(font);
    tft.setTextDatum(lgfx::top_left);

    char linea[160] = {0};
    int usados = 0;
    const char* p = texto;

    while (*p && usados < maxLineas) {
        const char* fin = p;
        while (*fin && *fin != ' ') fin++;
        const int largo = (int)(fin - p);
        if (largo <= 0) { p = *fin ? fin + 1 : fin; continue; }

        char cand[160];
        if (linea[0]) snprintf(cand, sizeof(cand), "%s %.*s", linea, largo, p);
        else          snprintf(cand, sizeof(cand), "%.*s", largo, p);

        if (tft.textWidth(cand) <= maxW) {
            snprintf(linea, sizeof(linea), "%s", cand);
        } else if (linea[0]) {
            tft.drawString(linea, x, y + usados * lh);
            usados++;
            snprintf(linea, sizeof(linea), "%.*s", largo, p);
        } else {
            // Una sola palabra más ancha que la pantalla: se dibuja igual y se
            // sale por el margen antes que tragarse el aviso entero.
            tft.drawString(cand, x, y + usados * lh);
            usados++;
            linea[0] = '\0';
        }
        p = *fin ? fin + 1 : fin;
    }
    if (linea[0] && usados < maxLineas) {
        tft.drawString(linea, x, y + usados * lh);
        usados++;
    }
    return usados;
}

void hace(char* out, size_t n, uint32_t ms) {
    const uint32_t s = (millis() - ms) / 1000;
    if (s < 10)    { snprintf(out, n, "recién"); return; }
    if (s < 60)    { snprintf(out, n, "hace %lu s", (unsigned long)s); return; }
    if (s < 3600)  { snprintf(out, n, "hace %lu min", (unsigned long)(s / 60)); return; }
    snprintf(out, n, "hace %lu h", (unsigned long)(s / 3600));
}

}  // namespace

void uiNotifSetup() { s_ready = true; }

void uiNotifDrawBarra(float restante01) {
    if (!s_ready) return;
    if (restante01 < 0.0f) restante01 = 0.0f;
    if (restante01 > 1.0f) restante01 = 1.0f;

    const int w = SCREEN_W - 2 * MARGIN;
    displayWaitVSync();
    tft.fillRect(MARGIN, BARRA_Y, w, BARRA_H, CANVAS);
    const int largo = (int)(w * restante01);
    if (largo > 0) tft.fillRect(MARGIN, BARRA_Y, largo, BARRA_H, FG_4);
}

void uiNotifDrawCard(const Notif* n, float restante01) {
    if (!s_ready || !n) return;

    uiAnimInvalidate();
    displayWaitVSync();
    tft.fillScreen(CANVAS);

    chip(n->src);

    // El título es lo único en tipografía grande: es lo que se lee de lejos, que
    // es cómo se mira un aviso.
    tft.setTextColor(FG, CANVAS);
    const int usados = envolver(n->title, MARGIN, TIT_Y, TIT_LH,
                                SCREEN_W - 2 * MARGIN, 2, DS::fontDataLg());

    if (n->body[0]) {
        tft.setTextColor(FG_3, CANVAS);
        const int y = TIT_Y + usados * TIT_LH + 30;
        envolver(n->body, MARGIN, y > CUE_Y ? y : CUE_Y, CUE_LH,
                 SCREEN_W - 2 * MARGIN, 3, DS::fontBody());
    }

    char cuando[24];
    hace(cuando, sizeof(cuando), n->ms);
    tft.setFont(DS::fontCaption());
    tft.setTextDatum(lgfx::top_left);
    tft.setTextColor(FG_4, CANVAS);
    tft.drawString(cuando, MARGIN, CUANDO_Y);
    tft.setTextDatum(lgfx::top_right);
    tft.drawString("tocá para cerrar", SCREEN_W - MARGIN, CUANDO_Y);

    uiNotifDrawBarra(restante01);
}

void uiNotifDrawLista() {
    if (!s_ready) return;

    uiAnimInvalidate();
    displayWaitVSync();
    tft.fillScreen(CANVAS);

    char rotulo[40];
    const uint8_t sinLeer = notifSinLeer();
    if (notifCount() == 0)  snprintf(rotulo, sizeof(rotulo), "AVISOS");
    else if (sinLeer == 0)  snprintf(rotulo, sizeof(rotulo), "AVISOS  ·  AL DÍA");
    else                    snprintf(rotulo, sizeof(rotulo), "AVISOS  ·  %u SIN LEER", sinLeer);
    chip(rotulo);

    s_filas = notifCount() < MAX_FILAS ? notifCount() : MAX_FILAS;

    if (notifCount() == 0) {
        tft.setTextDatum(lgfx::top_left);
        tft.setFont(DS::fontHeading());
        tft.setTextColor(FG_2, CANVAS);
        tft.drawString("Sin avisos.", MARGIN, 180);
        tft.setFont(DS::fontBody());
        tft.setTextColor(FG_3, CANVAS);
        tft.drawString("Los agentes avisan acá cuando", MARGIN, 222);
        tft.drawString("terminan una tarea.", MARGIN, 250);
        return;
    }

    for (uint8_t i = 0; i < s_filas; i++) {
        const Notif* n = notifAt(i);
        if (!n) continue;
        const int y = FILA_Y0 + i * FILA_H;

        // Punto lleno = sin leer. Es el mismo recurso del resto: contraste en
        // vez de color.
        if (!n->leido) tft.fillCircle(PUNTO_X, y + 14, 4, FG);
        else           tft.drawCircle(PUNTO_X, y + 14, 4, FG_4);

        tft.setTextDatum(lgfx::top_left);
        tft.setFont(DS::fontBody());
        tft.setTextColor(n->leido ? FG_4 : FG_3, CANVAS);
        tft.drawString(n->src, MARGIN + 22, y);

        char cuando[24];
        hace(cuando, sizeof(cuando), n->ms);
        tft.setTextDatum(lgfx::top_right);
        tft.drawString(cuando, SCREEN_W - MARGIN, y);

        tft.setTextDatum(lgfx::top_left);
        tft.setFont(DS::fontHeading());
        tft.setTextColor(n->leido ? FG_3 : FG, CANVAS);

        // Un solo renglón por aviso: la lista es para repasar, no para leer.
        char buf[NOTIF_TITLE_LEN + 4];
        const int maxW = SCREEN_W - MARGIN - (MARGIN + 22);
        if (tft.textWidth(n->title) <= maxW) {
            tft.drawString(n->title, MARGIN + 22, y + 24);
        } else {
            size_t cabe = 0;
            for (size_t k = 1; n->title[k] && k < sizeof(buf) - 4; k++) {
                snprintf(buf, sizeof(buf), "%.*s...", (int)k, n->title);
                if (tft.textWidth(buf) > maxW) break;
                cabe = k;
            }
            snprintf(buf, sizeof(buf), "%.*s...", (int)cabe, n->title);
            tft.drawString(buf, MARGIN + 22, y + 24);
        }
    }

    if (notifCount() > MAX_FILAS) {
        tft.setFont(DS::fontCaption());
        tft.setTextDatum(lgfx::top_left);
        tft.setTextColor(FG_4, CANVAS);
        char mas[32];
        snprintf(mas, sizeof(mas), "+%u más", (unsigned)(notifCount() - MAX_FILAS));
        tft.drawString(mas, MARGIN + 22, FILA_Y0 + MAX_FILAS * FILA_H);
    }
}

int8_t uiNotifHit(int16_t x, int16_t y) {
    if (x < 0 || x >= SCREEN_W) return -1;
    if (y < FILA_Y0) return -1;
    const int i = (y - FILA_Y0) / FILA_H;
    if (i < 0 || i >= s_filas) return -1;
    return (int8_t)i;
}

#include "ui_todo.h"
#include "config.h"
#include "design_system.h"
#include "ui_chrome.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;
using namespace FercedChrome;

// Se compone sobre el sprite, igual que las pantallas animadas, y se muestra
// con uiAnimReveal(). Antes esta pantalla dibujaba directo sobre tft y dejaba
// el sprite con contenido que ya no estaba en el panel, lo que obligaba a
// declarar uiAnimInvalidate() para que la siguiente animacion no empujara
// fantasmas. Componiendo siempre sobre el sprite el problema no existe.

namespace {

LGFX_Sprite& g = uiSprite;

constexpr int MARK_W = 11;

constexpr int FILA_Y0 = 100;
constexpr int FILA_H  = 46;
constexpr int MAX_FILAS = 7;          // 100 + 7*46 = 422, y el pie va en 428

constexpr int CAJA = 22;              // lado de la casilla
constexpr int CAJA_R = 7;
constexpr int CAJA_X = MARGIN;
constexpr int TEXTO_X = MARGIN + CAJA + 20;
constexpr int TEXTO_W = SCREEN_W - MARGIN - TEXTO_X;

constexpr int PIE_Y = 428;

uint8_t s_filas = 0;   // cuántas se dibujaron: sin esto un toque abajo del
                       // último ítem marcaría una tarea que no está en pantalla
bool    s_ready = false;

// Casilla vacía en contorno, hecha en negativo cuando está marcada. Es el mismo
// recurso que usa el selector para señalar la app activa: contraste en vez de
// color, que en esta identidad está reservado para estados.
void casilla(int x, int y, bool hecha) {
    if (hecha) {
        g.fillSmoothRoundRect(x, y, CAJA, CAJA, CAJA_R, FG);
        // Un tilde dibujado a mano: dos líneas gruesas. Las fuentes no tienen
        // el glifo, y traerlo sólo para esto no se justifica.
        for (int t = 0; t < 2; t++) {
            g.drawLine(x + 5, y + 11 + t, x + 9, y + 15 + t, CANVAS);
            g.drawLine(x + 9, y + 15 + t, x + 16, y + 6 + t, CANVAS);
        }
    } else {
        g.fillSmoothRoundRect(x, y, CAJA, CAJA, CAJA_R, FG_4);
        g.fillSmoothRoundRect(x + 2, y + 2, CAJA - 4, CAJA - 4, CAJA_R - 1, CANVAS);
    }
}

}  // namespace

void uiTodoSetup() { uiAnimSetup(); s_ready = true; }

uint8_t uiTodoVisibles() { return MAX_FILAS; }

void uiTodoDraw(const char* direccion) {
    if (!s_ready) return;

    g.fillScreen(CANVAS);

    const uint8_t total = todoCount();
    const uint8_t pend = todoPending();
    const uint8_t hechas = total - pend;

    char cuenta[32] = {0};
    if (total == 0)      snprintf(cuenta, sizeof(cuenta), "VACÍA");
    else if (pend == 0)  snprintf(cuenta, sizeof(cuenta), "TODO HECHO");
    else if (pend == 1)  snprintf(cuenta, sizeof(cuenta), "1 PENDIENTE");
    else                 snprintf(cuenta, sizeof(cuenta), "%u PENDIENTES", pend);

    uiEyebrow(g, "TAREAS", cuenta, 1.0f);
    uiMark(g, SCREEN_W - MARGIN - MARK_W, 20, 1.0f);

    s_filas = total < MAX_FILAS ? total : MAX_FILAS;

    if (total == 0) {
        // El estado vacío no es un error, así que se dice con la voz de la
        // marca: la cursiva con gracias, la misma que ferced.com usa para la
        // palabra que lleva el peso de cada título.
        g.setTextDatum(lgfx::top_left);
        g.setFont(DS::fontAcento());
        g.setTextColor(FG_2, CANVAS);
        g.drawString("nada pendiente.", MARGIN, 190);
        g.setFont(DS::fontBody());
        g.setTextColor(FG_3, CANVAS);
        g.drawString("Agregalas desde el teléfono:", MARGIN, 250);
        g.setTextColor(FG, CANVAS);
        g.drawString(direccion ? direccion : "", MARGIN, 278);
        uiRail(g, RAIL_Y, 0.0f);
        uiAnimReveal();
        return;
    }

    for (uint8_t i = 0; i < s_filas; i++) {
        const TodoItem* it = todoItem(i);
        if (!it) continue;
        const int y = FILA_Y0 + i * FILA_H;

        casilla(CAJA_X, y + (FILA_H - CAJA) / 2 - 4, it->done);

        g.setTextDatum(lgfx::top_left);
        g.setFont(DS::fontHeading());
        g.setTextColor(it->done ? FG_4 : FG, CANVAS);
        const int ty = y + 6;
        const int w = uiTextoRecortado(g, it->title, TEXTO_X, ty, TEXTO_W);

        // Tachado: la mitad del renglón, del ancho real del texto dibujado.
        if (it->done && w > 0) g.drawFastHLine(TEXTO_X, ty + 13, w, FG_4);
    }

    // Las que no entraron. Callarlas sería mentir sobre cuántas hay.
    if (total > MAX_FILAS) {
        g.setFont(DS::fontCaption());
        g.setTextDatum(lgfx::top_left);
        g.setTextColor(FG_4, CANVAS);
        char mas[32];
        snprintf(mas, sizeof(mas), "+%u más", (unsigned)(total - MAX_FILAS));
        g.drawString(mas, TEXTO_X, FILA_Y0 + MAX_FILAS * FILA_H - 12);
    }

    g.setFont(DS::fontCaption());
    g.setTextDatum(lgfx::top_left);
    g.setTextColor(FG_4, CANVAS);
    g.drawString(direccion ? direccion : "", MARGIN, PIE_Y);
    g.setTextDatum(lgfx::top_right);
    g.drawString("tocá una tarea para marcarla", SCREEN_W - MARGIN, PIE_Y);

    // El riel del pie mide lo hecho sobre el total. En noticias marca cuánto
    // falta para el próximo titular; acá, cuánto llevás. Es el mismo elemento en
    // el mismo renglón, midiendo lo que cada pantalla tiene para medir.
    uiRail(g, RAIL_Y, total > 0 ? (float)hechas / (float)total : 0.0f);
    uiAnimReveal();
}

int8_t uiTodoHit(int16_t x, int16_t y) {
    if (x < 0 || x >= SCREEN_W) return -1;
    if (y < FILA_Y0) return -1;
    const int i = (y - FILA_Y0) / FILA_H;
    if (i < 0 || i >= s_filas) return -1;
    return (int8_t)i;
}

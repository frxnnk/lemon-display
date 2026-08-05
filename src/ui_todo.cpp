#include "ui_todo.h"
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

constexpr int FILA_Y0 = 96;
constexpr int FILA_H  = 46;
constexpr int MAX_FILAS = 7;          // 96 + 7*46 = 418, y el pie va en 440

constexpr int CAJA = 22;              // lado de la casilla
constexpr int CAJA_R = 7;
constexpr int CAJA_X = MARGIN;
constexpr int TEXTO_X = MARGIN + CAJA + 20;
constexpr int TEXTO_W = SCREEN_W - MARGIN - TEXTO_X;

constexpr int PIE_Y = 440;

uint8_t s_filas = 0;   // cuántas se dibujaron: sin esto un toque abajo del
                       // último ítem marcaría una tarea que no está en pantalla
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

// Recorta con puntos suspensivos midiendo el candidato ya con los puntos, para
// que el resultado nunca se pase. Misma técnica que la pantalla de
// configuración, donde los SSID largos se iban del margen.
void dibujarRecortado(int x, int y, int ancho, const char* texto, const lgfx::IFont* font) {
    if (tft.textWidth(texto, font) <= ancho) {
        tft.drawString(texto, x, y, font);
        return;
    }
    char buf[TODO_TEXT_LEN + 4];
    size_t cabe = 0;
    for (size_t n = 1; texto[n] != '\0' && n < sizeof(buf) - 4; n++) {
        snprintf(buf, sizeof(buf), "%.*s...", (int)n, texto);
        if (tft.textWidth(buf, font) > ancho) break;
        cabe = n;
    }
    snprintf(buf, sizeof(buf), "%.*s...", (int)cabe, texto);
    tft.drawString(buf, x, y, font);
}

// Casilla vacía en contorno, hecha en negativo cuando está marcada. Es el mismo
// recurso que usa el selector para señalar la app activa: contraste en vez de
// color, que en esta identidad está reservado para estados.
void casilla(int x, int y, bool hecha) {
    if (hecha) {
        tft.fillSmoothRoundRect(x, y, CAJA, CAJA, CAJA_R, FG);
        // Un tilde dibujado a mano: dos líneas gruesas. Las fuentes no tienen
        // el glifo, y traerlo sólo para esto no se justifica.
        for (int g = 0; g < 2; g++) {
            tft.drawLine(x + 5, y + 11 + g, x + 9, y + 15 + g, CANVAS);
            tft.drawLine(x + 9, y + 15 + g, x + 16, y + 6 + g, CANVAS);
        }
    } else {
        tft.fillSmoothRoundRect(x, y, CAJA, CAJA, CAJA_R, FG_4);
        tft.fillSmoothRoundRect(x + 2, y + 2, CAJA - 4, CAJA - 4, CAJA_R - 1, CANVAS);
    }
}

}  // namespace

void uiTodoSetup() { s_ready = true; }

uint8_t uiTodoVisibles() { return MAX_FILAS; }

void uiTodoDraw(const char* direccion) {
    if (!s_ready) return;

    displayWaitVSync();
    tft.fillScreen(CANVAS);

    // Chip: cuántas quedan, que es lo único que importa de un vistazo.
    char chip[40];
    const uint8_t pend = todoPending();
    if (todoCount() == 0)   snprintf(chip, sizeof(chip), "TAREAS");
    else if (pend == 0)     snprintf(chip, sizeof(chip), "TAREAS  ·  TODO HECHO");
    else if (pend == 1)     snprintf(chip, sizeof(chip), "TAREAS  ·  1 PENDIENTE");
    else                    snprintf(chip, sizeof(chip), "TAREAS  ·  %u PENDIENTES", pend);

    tft.setFont(DS::fontCaption());
    const int chipW = tft.textWidth(chip) + 26;
    tft.fillSmoothRoundRect(MARGIN, CHIP_Y, chipW, CHIP_H, CHIP_H / 2, SURFACE);
    tft.setTextDatum(lgfx::middle_left);
    tft.setTextColor(FG_3, SURFACE);
    tft.drawString(chip, MARGIN + 13, CHIP_Y + CHIP_H / 2);

    marca(SCREEN_W - MARGIN - MARK_W, CHIP_Y);

    s_filas = todoCount() < MAX_FILAS ? todoCount() : MAX_FILAS;

    if (todoCount() == 0) {
        tft.setTextDatum(lgfx::top_left);
        tft.setFont(DS::fontHeading());
        tft.setTextColor(FG_2, CANVAS);
        tft.drawString("Sin tareas.", MARGIN, 180);
        tft.setFont(DS::fontBody());
        tft.setTextColor(FG_3, CANVAS);
        tft.drawString("Agregalas desde el teléfono:", MARGIN, 222);
        tft.setTextColor(FG, CANVAS);
        tft.drawString(direccion ? direccion : "", MARGIN, 250);
        return;
    }

    for (uint8_t i = 0; i < s_filas; i++) {
        const TodoItem* it = todoItem(i);
        if (!it) continue;
        const int y = FILA_Y0 + i * FILA_H;

        casilla(CAJA_X, y + (FILA_H - CAJA) / 2 - 4, it->done);

        tft.setTextDatum(lgfx::top_left);
        tft.setTextColor(it->done ? FG_4 : FG, CANVAS);
        const int ty = y + 6;
        dibujarRecortado(TEXTO_X, ty, TEXTO_W, it->text, DS::fontHeading());

        // Tachado: la mitad del renglón, del ancho real del texto dibujado.
        if (it->done) {
            int w = tft.textWidth(it->text, DS::fontHeading());
            if (w > TEXTO_W) w = TEXTO_W;
            tft.drawFastHLine(TEXTO_X, ty + 13, w, FG_4);
        }
    }

    // Las que no entraron. Callarlas sería mentir sobre cuántas hay.
    if (todoCount() > MAX_FILAS) {
        tft.setFont(DS::fontBody());
        tft.setTextDatum(lgfx::top_left);
        tft.setTextColor(FG_4, CANVAS);
        char mas[32];
        snprintf(mas, sizeof(mas), "+%u más", (unsigned)(todoCount() - MAX_FILAS));
        tft.drawString(mas, TEXTO_X, FILA_Y0 + MAX_FILAS * FILA_H + 2);
    }

    tft.setFont(DS::fontCaption());
    tft.setTextDatum(lgfx::top_left);
    tft.setTextColor(FG_4, CANVAS);
    tft.drawString(direccion ? direccion : "", MARGIN, PIE_Y);
    tft.setTextDatum(lgfx::top_right);
    tft.drawString("tocá una tarea para marcarla", SCREEN_W - MARGIN, PIE_Y);
}

int8_t uiTodoHit(int16_t x, int16_t y) {
    if (x < 0 || x >= SCREEN_W) return -1;
    if (y < FILA_Y0) return -1;
    const int i = (y - FILA_Y0) / FILA_H;
    if (i < 0 || i >= s_filas) return -1;
    return (int8_t)i;
}

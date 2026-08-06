#include "ui_chrome.h"

#include "config.h"
#include "design_system.h"
#include "data/ferced_mark_11.h"
#include "data/ferced_mark_c26.h"
#include "data/marca_claude_20.h"
#include "data/marca_claude_34.h"
#include "data/marca_claude_56.h"
#include "data/marca_codex_20.h"
#include "data/marca_codex_34.h"
#include "data/marca_codex_56.h"
#include "notif_store.h"        // NOTIF_SRC_LEN

#include <Arduino.h>
#include <cctype>
#include <cstring>

using namespace FercedColors;
using namespace FercedChrome;

namespace {

constexpr int MARK_W = 11;
constexpr int MARK_H = 28;

// Los tres tramos del isotipo, en el orden en que se leen de arriba hacia
// abajo. Son los valores medios de cada degrade del SVG: los extremos claros
// se lavan sobre negro y los oscuros no se ven a dos pixeles de alto.
constexpr uint32_t SPEC_0 = 0x3FD95A;   // verde
constexpr uint32_t SPEC_1 = 0x26CFC8;   // cyan
constexpr uint32_t SPEC_2 = 0x2C97E6;   // azul

constexpr uint16_t pack(uint32_t rgb) {
    return (uint16_t)((((rgb >> 16) & 0xFF) >> 3) << 11 |
                      (((rgb >> 8) & 0xFF) >> 2) << 5 |
                      (((rgb) & 0xFF) >> 3));
}

uint16_t mezcla(uint32_t a, uint32_t b, float t) {
    const int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    const int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    const uint32_t r = (uint32_t)(ar + (br - ar) * t);
    const uint32_t g = (uint32_t)(ag + (bg - ag) * t);
    const uint32_t bl = (uint32_t)(ab + (bb - ab) * t);
    return pack((r << 16) | (g << 8) | bl);
}

}  // namespace

uint16_t fercedSpectrum(float t) {
    if (t <= 0.0f) return pack(SPEC_0);
    if (t >= 1.0f) return pack(SPEC_2);
    // Se interpola en 8 bits por canal y recien despues se empaqueta: mezclar
    // en RGB565 directo pierde los pasos intermedios del verde, que tiene 6
    // bits, y el degrade sale escalonado.
    if (t < 0.5f) return mezcla(SPEC_0, SPEC_1, t * 2.0f);
    return mezcla(SPEC_1, SPEC_2, (t - 0.5f) * 2.0f);
}

void uiRail(LGFX_Sprite& g, int y, float filled01, float alpha) {
    uiRailEn(g, MARGIN, SCREEN_W - 2 * MARGIN, y, filled01, alpha);
}

void uiRailEn(LGFX_Sprite& g, int x0, int w, int y, float filled01, float alpha) {
    if (alpha <= 0.0f || w <= 1) return;
    if (filled01 < 0.0f) filled01 = 0.0f;
    if (filled01 > 1.0f) filled01 = 1.0f;
    const int filled = (int)(w * filled01 + 0.5f);

    // El tramo que falta queda como regla apagada: el riel siempre esta, lo que
    // cambia es cuanto esta encendido.
    g.fillRect(x0, y, w, RAIL_H, uiAnimLerp(LINE, CANVAS, alpha));

    // El espectro se estira sobre el tramo cumplido, no sobre el ancho total.
    // Repartido sobre los 424 px, un riel al 20% mostraba solo verde y la marca
    // no se leia; asi el degrade entero esta presente desde el primer pixel y lo
    // que crece es la escala. El filo siempre termina en el azul del isotipo.
    for (int i = 0; i < filled; i++) {
        const float t = filled > 1 ? (float)i / (float)(filled - 1) : 1.0f;
        g.drawFastVLine(x0 + i, y, RAIL_H, uiAnimLerp(fercedSpectrum(t), CANVAS, alpha));
    }
}

void uiEyebrow(LGFX_Sprite& g, const char* izq, const char* der, float alpha) {
    if (alpha <= 0.0f) return;

    g.setFont(DS::fontCaption());
    if (izq && izq[0]) {
        g.setTextDatum(lgfx::middle_left);
        g.setTextColor(uiAnimLerp(FG_3, CANVAS, alpha));
        g.drawString(izq, MARGIN, EYEBROW_Y);
    }
    if (der && der[0]) {
        g.setTextDatum(lgfx::middle_right);
        g.setTextColor(uiAnimLerp(FG_4, CANVAS, alpha));
        // Deja libre la columna de la marca, que vive pegada al margen.
        g.drawString(der, SCREEN_W - MARGIN - MARK_W - 16, EYEBROW_Y);
    }
    g.drawFastHLine(MARGIN, RULE_Y, SCREEN_W - 2 * MARGIN,
                    uiAnimLerp(LINE, CANVAS, alpha));
}

void uiMark(LGFX_Sprite& g, int x, int y, float alpha) {
    if (alpha <= 0.0f) return;
    for (int py = 0; py < MARK_H; py++) {
        for (int px = 0; px < MARK_W; px++) {
            const uint16_t c = pgm_read_word(&ferced_mark_11[py * MARK_W + px]);
            if (c != 0x0000) g.drawPixel(x + px, y + py, uiAnimLerp(c, CANVAS, alpha));
        }
    }
}

void uiMarkColor(LGFX_Sprite& g, int x, int y) {
    for (int py = 0; py < UI_MARK_C_H; py++) {
        for (int px = 0; px < UI_MARK_C_W; px++) {
            const uint16_t c = pgm_read_word(&ferced_mark_c26[py * UI_MARK_C_W + px]);
            if (c != 0x0000) g.drawPixel(x + px, y + py, c);
        }
    }
}

const MarcaAgente* marcaDeAgente(const char* src) {
    if (!src || !src[0]) return nullptr;

    // Coral #D97757, que es el color de la marca de Claude. En blanco el
    // destello se lee como un asterisco cualquiera; el color es lo que lo hace
    // reconocible de reojo, que es como se mira este aparato.
    static const MarcaAgente CLAUDE = {marca_claude_20, marca_claude_34,
                                      marca_claude_56, 0xDBAA};
    // El nudo de OpenAI es negro sobre blanco, o sea invisible sobre el canvas.
    // Blanco es su tratamiento para fondo oscuro.
    static const MarcaAgente CODEX  = {marca_codex_20, marca_codex_34,
                                      marca_codex_56, 0xFFFF};

    char norm[NOTIF_SRC_LEN];
    size_t n = 0;
    for (const char* p = src; *p && n < sizeof(norm) - 1; p++) {
        norm[n++] = (char)tolower((unsigned char)*p);
    }
    norm[n] = '\0';

    if (strstr(norm, "claude") || strstr(norm, "anthropic")) return &CLAUDE;
    if (strstr(norm, "codex") || strstr(norm, "openai") ||
        strstr(norm, "chatgpt") || strstr(norm, "gpt")) return &CODEX;
    return nullptr;
}

void uiMarcaAgente(LGFX_Sprite& g, const uint8_t* alfa, int lado, int x, int y,
                   uint16_t color, uint16_t fondo, float intensidad) {
    if (!alfa || lado <= 0) return;
    if (intensidad < 0.0f) intensidad = 0.0f;
    if (intensidad > 1.0f) intensidad = 1.0f;
    for (int py = 0; py < lado; py++) {
        for (int px = 0; px < lado; px++) {
            const uint8_t a = pgm_read_byte(&alfa[py * lado + px]);
            if (a == 0) continue;
            const float t = (float)a / 255.0f * intensidad;
            g.drawPixel(x + px, y + py, uiAnimLerp(color, fondo, t));
        }
    }
}

void uiBurbuja(LGFX_Sprite& g, int x, int y, int w, int h,
               uint16_t borde, uint16_t relleno) {
    const int r = h / 3 < 14 ? h / 3 : 14;
    g.fillSmoothRoundRect(x, y, w, h, r, borde);
    g.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, relleno);

    // La cola, abajo a la izquierda. Se arma con dos triangulos concentricos
    // por el mismo motivo que los bordes de 1 px: LovyanGFX solo sabe rellenar.
    // El de adentro arranca un pixel mas arriba del borde inferior del cuerpo
    // para que no quede una costura entre la cola y la burbuja.
    const int bx = x + r;
    const int by = y + h - 2;
    g.fillTriangle(bx, by, bx + 15, by, bx + 3, by + UI_BURBUJA_COLA, borde);
    g.fillTriangle(bx + 1, by - 1, bx + 12, by - 1, bx + 4, by + UI_BURBUJA_COLA - 3, relleno);
}

void uiRoundCorners(LGFX_Sprite& g, int x, int y, int w, int h, int r,
                    uint16_t fondo) {
    if (r <= 0) return;
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            // Distancia al centro del arco. Medio pixel de corrimiento para que
            // la curva quede centrada en el pixel y no en su borde.
            const float ddx = (float)(r - dx) - 0.5f;
            const float ddy = (float)(r - dy) - 0.5f;
            if (ddx * ddx + ddy * ddy <= (float)(r * r)) continue;
            g.drawPixel(x + dx,             y + dy,             fondo);
            g.drawPixel(x + w - 1 - dx,     y + dy,             fondo);
            g.drawPixel(x + dx,             y + h - 1 - dy,     fondo);
            g.drawPixel(x + w - 1 - dx,     y + h - 1 - dy,     fondo);
        }
    }
}

int uiTextoRecortado(LGFX_Sprite& g, const char* texto, int x, int y, int maxW) {
    if (!texto || !texto[0]) return 0;
    if (g.textWidth(texto) <= maxW) {
        g.drawString(texto, x, y);
        return g.textWidth(texto);
    }
    // Se recorta por ancho medido, no por cantidad de caracteres: con fuente
    // proporcional contar caracteres deja unos renglones cortos y otros al filo.
    char buf[96];
    const int n = (int)strlen(texto);
    for (int len = n - 1; len > 0; len--) {
        const int corte = len < (int)sizeof(buf) - 4 ? len : (int)sizeof(buf) - 4;
        memcpy(buf, texto, corte);
        strcpy(buf + corte, "...");
        if (g.textWidth(buf) <= maxW) {
            g.drawString(buf, x, y);
            return g.textWidth(buf);
        }
    }
    return 0;
}

#include "ui_ferced.h"
#include "display_manager.h"
#include "config.h"
#include "design_system.h"
#include "data/ferced_mark_16.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;

static LGFX_Sprite s_sprite(&tft);
static bool s_ready = false;

static constexpr int MARK_W = 16;
static constexpr int MARK_H = 40;

static constexpr int MARGIN    = 24;
static constexpr int CHIP_Y    = 26;
static constexpr int CHIP_H    = 30;
static constexpr int CARD_X    = MARGIN;
static constexpr int CARD_Y    = 96;
static constexpr int CARD_W    = SCREEN_W - 2 * MARGIN;
static constexpr int CARD_H    = 300;
static constexpr int CARD_PAD  = 24;
static constexpr int TEXT_LH   = 34;
static constexpr int MAX_LINES = 5;
static constexpr int DOTS_Y    = 436;

void uiFercedSetup() {
    if (s_ready) return;
    s_sprite.setPsram(true);
    s_sprite.setColorDepth(16);
    s_sprite.createSprite(SCREEN_W, SCREEN_H);
    s_ready = true;
}

static void drawMark(int x, int y) {
    for (int py = 0; py < MARK_H; py++) {
        for (int px = 0; px < MARK_W; px++) {
            const uint16_t c = pgm_read_word(&ferced_mark_16[py * MARK_W + px]);
            if (c != 0x0000) s_sprite.drawPixel(x + px, y + py, c);
        }
    }
}

// El wordmark de Ferced es la palabra en minusculas con tracking cerrado.
static void drawWordmark(int right, int y) {
    const char* w = "ferced";
    s_sprite.setFont(DS::fontHeading());
    s_sprite.setTextDatum(lgfx::top_right);
    s_sprite.setTextColor(FG, CANVAS);
    s_sprite.drawString(w, right, y);
}

static void drawChip(const char* label) {
    s_sprite.setFont(&fonts::Font0);
    s_sprite.setTextDatum(lgfx::middle_left);
    const int textW = s_sprite.textWidth(label);
    const int w = textW + 28;
    s_sprite.fillSmoothRoundRect(MARGIN, CHIP_Y, w, CHIP_H, CHIP_H / 2, SURFACE);
    s_sprite.setTextColor(FG_3, SURFACE);
    s_sprite.drawString(label, MARGIN + 14, CHIP_Y + CHIP_H / 2);
}

// Corta por ancho real en pixeles: contar caracteres con fuente proporcional
// deja renglones desparejos.
static int wrapText(const char* text, char lines[MAX_LINES][64], int maxW) {
    int count = 0;
    const char* p = text;
    char line[64] = {0};
    int lineLen = 0;

    while (*p && count < MAX_LINES) {
        const char* wordEnd = p;
        while (*wordEnd && *wordEnd != ' ') wordEnd++;
        const int wordLen = wordEnd - p;
        if (wordLen <= 0 || wordLen >= 63) { p = *wordEnd ? wordEnd + 1 : wordEnd; continue; }

        char candidate[64];
        if (lineLen > 0) {
            snprintf(candidate, sizeof(candidate), "%s %.*s", line, wordLen, p);
        } else {
            snprintf(candidate, sizeof(candidate), "%.*s", wordLen, p);
        }

        if (s_sprite.textWidth(candidate) <= maxW) {
            strncpy(line, candidate, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
            lineLen = strlen(line);
        } else {
            if (lineLen > 0) {
                strncpy(lines[count], line, 63);
                lines[count][63] = '\0';
                count++;
                snprintf(line, sizeof(line), "%.*s", wordLen, p);
                lineLen = strlen(line);
            } else {
                strncpy(lines[count], candidate, 63);
                lines[count][63] = '\0';
                count++;
                line[0] = '\0';
                lineLen = 0;
            }
        }
        p = *wordEnd ? wordEnd + 1 : wordEnd;
    }

    if (lineLen > 0 && count < MAX_LINES) {
        strncpy(lines[count], line, 63);
        lines[count][63] = '\0';
        count++;
    }
    return count;
}

// Se recalcula en cada dibujado: aunque el item se repita, el tiempo avanza y
// la pantalla no se siente congelada.
static void relativeTime(char* out, size_t len, uint32_t epoch, uint32_t now) {
    if (epoch == 0 || now == 0 || now < epoch) { snprintf(out, len, "recien"); return; }
    const uint32_t diff = now - epoch;
    if (diff < 60)    { snprintf(out, len, "recien"); return; }
    if (diff < 3600)  { snprintf(out, len, "hace %lu min", (unsigned long)(diff / 60)); return; }
    if (diff < 86400) { snprintf(out, len, "hace %lu h", (unsigned long)(diff / 3600)); return; }
    snprintf(out, len, "hace %lu d", (unsigned long)(diff / 86400));
}

static const char* originLabel(const FeedItem* it) {
    switch (it->origin) {
        case FEED_FROM_X:     return it->handle[0] ? it->handle : "X";
        case FEED_FROM_TREND: return "TENDENCIA";
        default:              return it->handle[0] ? it->handle : "NOTICIAS";
    }
}

static void drawDots(uint8_t index, uint8_t total) {
    if (total < 2) return;
    const uint8_t shown = total > 12 ? 12 : total;
    const int gap = 14;
    const int startX = SCREEN_W / 2 - ((shown - 1) * gap) / 2;
    const uint8_t active = total > 12 ? (uint8_t)((uint32_t)index * shown / total) : index;
    for (uint8_t i = 0; i < shown; i++) {
        const bool on = (i == active);
        s_sprite.fillCircle(startX + i * gap, DOTS_Y, on ? 3 : 2, on ? FG : FG_4);
    }
}

static void drawFrame(bool offline) {
    s_sprite.fillScreen(CANVAS);
    drawMark(SCREEN_W - MARGIN - MARK_W, CHIP_Y);
    drawWordmark(SCREEN_W - MARGIN - MARK_W - 10, CHIP_Y + 10);
    if (offline) {
        s_sprite.fillCircle(MARGIN + 6, DOTS_Y, 3, DANGER);
    }
}

void uiFercedDrawItem(const FeedItem* item, uint8_t index, uint8_t total,
                      uint32_t nowEpoch, bool offline) {
    if (!s_ready || !item) return;

    drawFrame(offline);

    char chip[64];
    snprintf(chip, sizeof(chip), "AHORA  %s", originLabel(item));
    for (char* c = chip; *c; c++) *c = toupper((unsigned char)*c);
    drawChip(chip);

    s_sprite.fillSmoothRoundRect(CARD_X, CARD_Y, CARD_W, CARD_H, 16, CARD);
    s_sprite.drawRoundRect(CARD_X, CARD_Y, CARD_W, CARD_H, 16, LINE);

    s_sprite.setFont(DS::fontHeading());
    char lines[MAX_LINES][64];
    const int n = wrapText(item->text, lines, CARD_W - 2 * CARD_PAD);

    s_sprite.setTextDatum(lgfx::top_left);
    s_sprite.setTextColor(FG, CARD);
    for (int i = 0; i < n; i++) {
        s_sprite.drawString(lines[i], CARD_X + CARD_PAD, CARD_Y + CARD_PAD + i * TEXT_LH);
    }

    const int footY = CARD_Y + CARD_H - CARD_PAD - 44;
    if (item->author[0]) {
        s_sprite.setFont(DS::fontBody());
        s_sprite.setTextColor(FG_2, CARD);
        s_sprite.drawString(item->author, CARD_X + CARD_PAD, footY);
    }

    char when[32];
    relativeTime(when, sizeof(when), item->epoch, nowEpoch);
    s_sprite.setFont(DS::fontCaption());
    s_sprite.setTextColor(FG_3, CARD);
    s_sprite.drawString(when, CARD_X + CARD_PAD, footY + 24);

    drawDots(index, total);
    s_sprite.pushSprite(0, 0);
}

void uiFercedDrawStatus(const char* eyebrow, const char* message) {
    if (!s_ready) return;

    drawFrame(false);

    char chip[64];
    snprintf(chip, sizeof(chip), "%s", eyebrow ? eyebrow : "");
    for (char* c = chip; *c; c++) *c = toupper((unsigned char)*c);
    drawChip(chip);

    s_sprite.setFont(DS::fontHeading());
    char lines[MAX_LINES][64];
    const int n = wrapText(message ? message : "", lines, CARD_W - 2 * CARD_PAD);

    s_sprite.setTextDatum(lgfx::top_left);
    s_sprite.setTextColor(FG_2, CANVAS);
    const int y0 = SCREEN_H / 2 - (n * TEXT_LH) / 2;
    for (int i = 0; i < n; i++) {
        s_sprite.drawString(lines[i], CARD_X + CARD_PAD, y0 + i * TEXT_LH);
    }

    s_sprite.pushSprite(0, 0);
}

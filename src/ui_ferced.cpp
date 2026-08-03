#include "ui_ferced.h"
#include "display_manager.h"
#include "config.h"
#include "design_system.h"
#include "data/ferced_mark_11.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;

static LGFX_Sprite s_sprite(&tft);
static bool s_ready = false;

// ── Geometría ──
static constexpr int MARK_W = 11;
static constexpr int MARK_H = 28;

static constexpr int MARGIN    = 28;
static constexpr int CHIP_Y    = 30;
static constexpr int CHIP_H    = 28;
static constexpr int TEXT_Y    = 118;
static constexpr int TEXT_LH   = 38;
static constexpr int MAX_LINES = 5;
static constexpr int LINE_W    = 64;
static constexpr int FOOT_Y    = 356;
static constexpr int PROG_Y    = 452;

// ── Animación ──
// Duración corta y stagger chico: la sensación de "caro" viene de que los
// elementos no entren todos juntos, no de que tarde.
static constexpr uint16_t ENTER_MS   = 460;
static constexpr uint16_t STAGGER_MS = 55;
static constexpr int      RISE_PX    = 26;
static constexpr uint8_t  SLOTS      = 5;   // chip, texto(3 grupos), pie

// easeOutQuint aproxima cubic-bezier(.16, 1, .3, 1), el easing de la marca:
// arranca rápido y frena largo. Barato de calcular en un MCU.
static inline float easeOut(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv * inv * inv;
}

// RGB565 no tiene alfa: los fundidos se hacen mezclando contra el fondo.
static uint16_t lerp565(uint16_t fg, uint16_t bg, float t) {
    if (t >= 1.0f) return fg;
    if (t <= 0.0f) return bg;
    const int fr = (fg >> 11) & 0x1F, fgr = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    const int br = (bg >> 11) & 0x1F, bgr = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    const int r = br + (int)((fr - br) * t);
    const int g = bgr + (int)((fgr - bgr) * t);
    const int b = bb + (int)((fb - bb) * t);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// ── Estado del ítem en pantalla ──
struct Slide {
    char     lines[MAX_LINES][64];
    int      lineCount;
    char     chip[48];
    char     author[FEED_AUTHOR_LEN];
    char     when[32];
    uint32_t epoch;
    const uint16_t* img;
    uint8_t  index;
    uint8_t  total;
    bool     offline;
    bool     isStatus;
};

static Slide    s_cur;
static uint32_t s_animStart = 0;
static bool     s_animating = false;
static bool     s_hasContent = false;
static float    s_progress = 0.0f;
static uint32_t s_lastProgressPaint = 0;

void uiFercedSetup() {
    if (s_ready) return;
    s_sprite.setPsram(true);
    s_sprite.setColorDepth(16);
    s_sprite.createSprite(SCREEN_W, SCREEN_H);
    memset(&s_cur, 0, sizeof(s_cur));
    s_ready = true;
}

// ── Primitivas ──

static void drawMark(int x, int y, float alpha) {
    for (int py = 0; py < MARK_H; py++) {
        for (int px = 0; px < MARK_W; px++) {
            const uint16_t c = pgm_read_word(&ferced_mark_11[py * MARK_W + px]);
            if (c != 0x0000) {
                s_sprite.drawPixel(x + px, y + py, lerp565(c, CANVAS, alpha));
            }
        }
    }
}

// Corta por ancho real en píxeles: contar caracteres con fuente proporcional
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
        if (lineLen > 0) snprintf(candidate, sizeof(candidate), "%s %.*s", line, wordLen, p);
        else             snprintf(candidate, sizeof(candidate), "%.*s", wordLen, p);

        if (s_sprite.textWidth(candidate) <= maxW) {
            strncpy(line, candidate, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
            lineLen = strlen(line);
        } else if (lineLen > 0) {
            strncpy(lines[count], line, 63); lines[count][63] = '\0'; count++;
            snprintf(line, sizeof(line), "%.*s", wordLen, p);
            lineLen = strlen(line);
        } else {
            strncpy(lines[count], candidate, 63); lines[count][63] = '\0'; count++;
            line[0] = '\0'; lineLen = 0;
        }
        p = *wordEnd ? wordEnd + 1 : wordEnd;
    }
    if (lineLen > 0 && count < MAX_LINES) {
        strncpy(lines[count], line, 63); lines[count][63] = '\0'; count++;
    }
    return count;
}

// Se recalcula en cada dibujado: aunque el ítem se repita, el tiempo avanza y
// la pantalla no se siente congelada.
static void relativeTime(char* out, size_t len, uint32_t epoch, uint32_t now) {
    if (epoch == 0 || now == 0 || now < epoch) { snprintf(out, len, "recien"); return; }
    const uint32_t d = now - epoch;
    if (d < 60)    { snprintf(out, len, "recien"); return; }
    if (d < 3600)  { snprintf(out, len, "hace %lu min", (unsigned long)(d / 60)); return; }
    if (d < 86400) { snprintf(out, len, "hace %lu h", (unsigned long)(d / 3600)); return; }
    snprintf(out, len, "hace %lu d", (unsigned long)(d / 86400));
}

static const char* sourceLabel(const FeedItem* it) {
    if (it->origin == FEED_FROM_X) return it->handle[0] ? it->handle : "X";
    if (it->author[0]) return it->author;
    return "NOTICIAS";
}

// Progreso de un elemento segun su turno en el stagger.
static float slotT(uint32_t elapsed, uint8_t slot) {
    const int32_t local = (int32_t)elapsed - (int32_t)slot * STAGGER_MS;
    if (local <= 0) return 0.0f;
    return easeOut((float)local / (float)ENTER_MS);
}

// ── Composición ──

static void paintFrame(uint32_t elapsed, uint32_t nowEpoch) {
    s_sprite.fillScreen(CANVAS);

    const float markT = slotT(elapsed, 0);
    drawMark(SCREEN_W - MARGIN - MARK_W, CHIP_Y, markT);

    // Chip de fuente
    const float chipT = slotT(elapsed, 0);
    if (chipT > 0.0f && s_cur.chip[0]) {
        const int dy = (int)((1.0f - chipT) * RISE_PX);
        s_sprite.setFont(&fonts::Font0);
        const int w = s_sprite.textWidth(s_cur.chip) + 26;
        s_sprite.fillSmoothRoundRect(MARGIN, CHIP_Y + dy, w, CHIP_H, CHIP_H / 2,
                                     lerp565(SURFACE, CANVAS, chipT));
        s_sprite.setTextDatum(lgfx::middle_left);
        s_sprite.setTextColor(lerp565(FG_3, CANVAS, chipT));
        s_sprite.drawString(s_cur.chip, MARGIN + 13, CHIP_Y + dy + CHIP_H / 2);
    }

    // Texto: los renglones entran de a uno, no en bloque.
    s_sprite.setFont(s_cur.isStatus ? DS::fontHeading() : DS::fontHeading());
    s_sprite.setTextDatum(lgfx::top_left);
    for (int i = 0; i < s_cur.lineCount; i++) {
        const uint8_t slot = 1 + (i * 2) / MAX_LINES;
        const float t = slotT(elapsed, slot);
        if (t <= 0.0f) continue;
        const int dy = (int)((1.0f - t) * RISE_PX);
        s_sprite.setTextColor(lerp565(s_cur.isStatus ? FG_2 : FG, CANVAS, t));
        s_sprite.drawString(s_cur.lines[i], MARGIN, TEXT_Y + i * TEXT_LH + dy);
    }

    if (s_cur.isStatus) return;

    // Pie: imagen, autor, tiempo
    const float footT = slotT(elapsed, 3);
    if (footT > 0.0f) {
        const int dy = (int)((1.0f - footT) * RISE_PX);
        int textX = MARGIN;

        if (s_cur.img) {
            // pushImage no mezcla, asi que el fundido se hace pixel a pixel.
            for (int py = 0; py < FEED_IMG_SIDE; py++) {
                for (int px = 0; px < FEED_IMG_SIDE; px++) {
                    s_sprite.drawPixel(MARGIN + px, FOOT_Y + dy + py,
                                       lerp565(s_cur.img[py * FEED_IMG_SIDE + px],
                                               CANVAS, footT));
                }
            }
            textX += FEED_IMG_SIDE + 18;
        }

        if (s_cur.author[0]) {
            s_sprite.setFont(DS::fontBody());
            s_sprite.setTextColor(lerp565(FG_2, CANVAS, footT));
            s_sprite.drawString(s_cur.author, textX, FOOT_Y + dy + 14);
        }
        s_sprite.setFont(DS::fontCaption());
        s_sprite.setTextColor(lerp565(FG_3, CANVAS, footT));
        s_sprite.drawString(s_cur.when, textX, FOOT_Y + dy + 40);

        if (s_cur.offline) {
            s_sprite.fillCircle(SCREEN_W - MARGIN - 4, FOOT_Y + dy + 20, 4,
                                lerp565(DANGER, CANVAS, footT));
        }
    }

    // Línea de progreso hacia el próximo ítem: da vida constante sin ruido.
    const float progT = slotT(elapsed, 4);
    if (progT > 0.0f) {
        const int w = SCREEN_W - 2 * MARGIN;
        s_sprite.drawFastHLine(MARGIN, PROG_Y, w, lerp565(LINE, CANVAS, progT));
        const int filled = (int)(w * s_progress);
        if (filled > 0) {
            s_sprite.drawFastHLine(MARGIN, PROG_Y, filled, lerp565(FG_4, CANVAS, progT));
        }
    }
}

static void present() {
    displayWaitVSync();
    const uint32_t t0 = micros();
    s_sprite.pushSprite(0, 0);
    displayRecordPush(SCREEN_W * SCREEN_H * 2, micros() - t0);
}

// ── API ──

static void beginSlide() {
    s_animStart = millis();
    s_animating = true;
    s_hasContent = true;
}

void uiFercedShowItem(const FeedItem* item, uint8_t index, uint8_t total,
                      bool offline) {
    if (!s_ready || !item) return;

    memset(&s_cur, 0, sizeof(s_cur));
    s_cur.isStatus = false;
    s_cur.index = index;
    s_cur.total = total;
    s_cur.offline = offline;
    s_cur.epoch = item->epoch;

    snprintf(s_cur.chip, sizeof(s_cur.chip), "%s", sourceLabel(item));
    for (char* c = s_cur.chip; *c; c++) *c = toupper((unsigned char)*c);
    strncpy(s_cur.author, item->author, FEED_AUTHOR_LEN - 1);

    s_sprite.setFont(DS::fontHeading());
    s_cur.lineCount = wrapText(item->text, s_cur.lines, SCREEN_W - 2 * MARGIN);

    // La imagen se baja una sola vez acá, nunca dentro de un frame de
    // animación: un GET de 8 KB en medio de la transición la cortaría.
    s_cur.img = feedFetchImage(item->imgKey);

    beginSlide();
}

void uiFercedShowStatus(const char* eyebrow, const char* message) {
    if (!s_ready) return;

    memset(&s_cur, 0, sizeof(s_cur));
    s_cur.isStatus = true;
    snprintf(s_cur.chip, sizeof(s_cur.chip), "%s", eyebrow ? eyebrow : "");
    for (char* c = s_cur.chip; *c; c++) *c = toupper((unsigned char)*c);

    s_sprite.setFont(DS::fontHeading());
    s_cur.lineCount = wrapText(message ? message : "", s_cur.lines,
                               SCREEN_W - 2 * MARGIN);
    beginSlide();
}

bool uiFercedTick(uint32_t nowEpoch, float progress01) {
    if (!s_ready || !s_hasContent) return false;

    const uint32_t now = millis();
    s_progress = progress01 < 0.0f ? 0.0f : (progress01 > 1.0f ? 1.0f : progress01);

    if (s_animating) {
        const uint32_t elapsed = now - s_animStart;
        relativeTime(s_cur.when, sizeof(s_cur.when), s_cur.epoch, nowEpoch);
        paintFrame(elapsed, nowEpoch);
        present();

        if (elapsed >= (uint32_t)ENTER_MS + (uint32_t)STAGGER_MS * SLOTS) {
            s_animating = false;
            s_lastProgressPaint = now;
        }
        return true;
    }

    // En reposo repinta 4 veces por segundo: alcanza para que la línea de
    // progreso avance suave y no tiene costo perceptible.
    if (now - s_lastProgressPaint >= 250) {
        s_lastProgressPaint = now;
        relativeTime(s_cur.when, sizeof(s_cur.when), s_cur.epoch, nowEpoch);
        paintFrame(0xFFFF, nowEpoch);
        present();
    }
    return false;
}

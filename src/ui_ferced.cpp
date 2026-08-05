#include "ui_ferced.h"
#include "config.h"
#include "design_system.h"
#include "data/ferced_mark_11.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;

// El sprite, el escalonado, la espera de VSync y la medicion viven en
// ui_anim.cpp: los comparten esta pantalla, la de padel y el launcher.

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
static constexpr int FOOT_Y    = 356;
static constexpr int PROG_Y    = 452;

// 0 = chip y marca, 1..5 = renglones de texto, 6 = pie, 7 = progreso.
static constexpr uint8_t SLOT_CHIP  = 0;
static constexpr uint8_t SLOT_LINE0 = 1;
static constexpr uint8_t SLOT_FOOT  = 6;
static constexpr uint8_t SLOT_PROG  = 7;
static constexpr uint8_t SLOTS      = 8;

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
static bool     s_animating = false;
static bool     s_hasContent = false;
static float    s_progress = 0.0f;
static uint32_t s_lastProgressPaint = 0;

// Geometría del ítem anterior. Sin esto, un titular más corto que el que estaba
// deja los renglones de abajo colgados en pantalla para siempre: sus bandas ya
// no se animan, así que nadie los repinta.
static int      s_prevLines = 0;
static bool     s_prevStatus = false;

void uiFercedSetup() {
    if (s_ready) return;
    uiAnimSetup();
    memset(&s_cur, 0, sizeof(s_cur));
    s_ready = true;
}

// ── Primitivas ──

static void drawMark(int x, int y, float alpha) {
    for (int py = 0; py < MARK_H; py++) {
        for (int px = 0; px < MARK_W; px++) {
            const uint16_t c = pgm_read_word(&ferced_mark_11[py * MARK_W + px]);
            if (c != 0x0000) {
                uiSprite.drawPixel(x + px, y + py, uiAnimLerp(c, CANVAS, alpha));
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

        if (uiSprite.textWidth(candidate) <= maxW) {
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

// ── Composición ──

static UiBand dirtyBand(uint32_t elapsed) {
    if (elapsed == 0xFFFF) {                 // repintado de reposo
        return UiBand{PROG_Y - 4, 10};       // solo la linea de progreso
    }
    const uint32_t total = uiAnimTotalMs(SLOTS);
    // Frame de cierre: todo lo anterior ya se empujo al llegar a su valor
    // final. Repintar la pantalla entera aca costaba 99 ms para nada.
    if (elapsed >= total) return UiBand{PROG_Y - 6, UI_RISE_PX + 12};

    int top = SCREEN_H, bottom = 0;
    // Un elemento esta "vivo" mientras su t no llego a 1. Cada renglon tiene
    // banda propia: asi la zona a repintar es de ~70 px, no de 480.
    for (uint8_t slot = 0; slot < SLOTS; slot++) {
        const float t = uiAnimSlotT(elapsed, slot);
        if (t <= 0.0f || t >= 1.0f) continue;
        int y0, y1;
        if (slot == SLOT_CHIP) {
            y0 = CHIP_Y - 4;
            y1 = CHIP_Y + CHIP_H + UI_RISE_PX;
        } else if (slot == SLOT_FOOT) {
            y0 = FOOT_Y - 4;
            y1 = FOOT_Y + FEED_IMG_SIDE + UI_RISE_PX;
        } else if (slot == SLOT_PROG) {
            y0 = PROG_Y - 6;
            y1 = PROG_Y + UI_RISE_PX + 6;
        } else {
            const int i = slot - SLOT_LINE0;
            y0 = TEXT_Y + i * TEXT_LH - 4;
            y1 = TEXT_Y + i * TEXT_LH + TEXT_LH + UI_RISE_PX;
        }
        if (y0 < top) top = y0;
        if (y1 > bottom) bottom = y1;
    }
    // Sin ningun elemento vivo no hay nada que repintar. Devolver la pantalla
    // entera aca era una trampa: con los slots en secuencia quedan huecos de
    // 10 ms entre uno y otro, y cada hueco costaba un repintado de 99 ms.
    if (bottom <= top) return UiBand{0, 0};
    return UiBand{top, bottom - top};
}

static UiBand paintFrame(uint32_t elapsed, uint32_t nowEpoch) {
    const UiBand b = dirtyBand(elapsed);
    if (b.h <= 0) return b;   // nada vivo: ni limpiar ni componer
    uiSprite.fillRect(0, b.y, SCREEN_W, b.h, CANVAS);

    const float markT = uiAnimSlotT(elapsed, SLOT_CHIP);
    const bool chipBand = uiAnimTouches(b, CHIP_Y - 4, CHIP_Y + CHIP_H + UI_RISE_PX);
    if (chipBand) drawMark(SCREEN_W - MARGIN - MARK_W, CHIP_Y, markT);

    // Chip de fuente
    const float chipT = markT;
    if (chipBand && chipT > 0.0f && s_cur.chip[0]) {
        const int dy = (int)((1.0f - chipT) * UI_RISE_PX);
        // Satoshi9 y no Font0: la fuente de la marca tiene el rango hasta 0xFF,
        // asi que el chip puede escribir acentos y el punto medio. Con Font0,
        // que es ASCII, "EN JUEGO · P1" salia con un glifo roto en el medio.
        uiSprite.setFont(DS::fontCaption());
        const int w = uiSprite.textWidth(s_cur.chip) + 26;
        uiSprite.fillSmoothRoundRect(MARGIN, CHIP_Y + dy, w, CHIP_H, CHIP_H / 2,
                                     uiAnimLerp(SURFACE, CANVAS, chipT));
        uiSprite.setTextDatum(lgfx::middle_left);
        uiSprite.setTextColor(uiAnimLerp(FG_3, CANVAS, chipT));
        uiSprite.drawString(s_cur.chip, MARGIN + 13, CHIP_Y + dy + CHIP_H / 2);
    }

    // Texto: los renglones entran de a uno, no en bloque.
    uiSprite.setFont(DS::fontHeading());
    uiSprite.setTextDatum(lgfx::top_left);
    for (int i = 0; i < s_cur.lineCount; i++) {
        const int ly = TEXT_Y + i * TEXT_LH;
        if (!uiAnimTouches(b, ly - 4, ly + TEXT_LH + UI_RISE_PX)) continue;
        const float t = uiAnimSlotT(elapsed, SLOT_LINE0 + i);
        if (t <= 0.0f) continue;
        const int dy = (int)((1.0f - t) * UI_RISE_PX);
        uiSprite.setTextColor(uiAnimLerp(s_cur.isStatus ? FG_2 : FG, CANVAS, t));
        uiSprite.drawString(s_cur.lines[i], MARGIN, TEXT_Y + i * TEXT_LH + dy);
    }

    if (s_cur.isStatus) return b;

    // Pie: imagen, autor, tiempo
    const float footT = uiAnimSlotT(elapsed, SLOT_FOOT);
    if (footT > 0.0f && uiAnimTouches(b, FOOT_Y - 4, FOOT_Y + FEED_IMG_SIDE + UI_RISE_PX)) {
        const int dy = (int)((1.0f - footT) * UI_RISE_PX);
        int textX = MARGIN;

        if (s_cur.img) {
            // pushImage copia por filas; el fundido pixel a pixel costaba 4096
            // llamadas a drawPixel por frame. La imagen entra por movimiento,
            // que es igual de elegante y practicamente gratis.
            // El cast es necesario: con un uint16_t* pelado LovyanGFX asume
            // orden intercambiado (el de SPI) y la imagen sale con los colores
            // rotos. El proxy escribe RGB565 en orden nativo.
            uiSprite.pushImage(MARGIN, FOOT_Y + dy,
                               FEED_IMG_SIDE, FEED_IMG_SIDE,
                               (const lgfx::rgb565_t*)s_cur.img);
            textX += FEED_IMG_SIDE + 18;
        }

        if (s_cur.author[0]) {
            uiSprite.setFont(DS::fontBody());
            uiSprite.setTextColor(uiAnimLerp(FG_2, CANVAS, footT));
            uiSprite.drawString(s_cur.author, textX, FOOT_Y + dy + 14);
        }
        uiSprite.setFont(DS::fontCaption());
        uiSprite.setTextColor(uiAnimLerp(FG_3, CANVAS, footT));
        uiSprite.drawString(s_cur.when, textX, FOOT_Y + dy + 40);

        if (s_cur.offline) {
            uiSprite.fillCircle(SCREEN_W - MARGIN - 4, FOOT_Y + dy + 20, 4,
                                uiAnimLerp(DANGER, CANVAS, footT));
        }
    }

    // Línea de progreso hacia el próximo ítem: da vida constante sin ruido.
    const float progT = uiAnimSlotT(elapsed, SLOT_PROG);
    if (progT > 0.0f) {
        const int w = SCREEN_W - 2 * MARGIN;
        uiSprite.drawFastHLine(MARGIN, PROG_Y, w, uiAnimLerp(LINE, CANVAS, progT));
        const int filled = (int)(w * s_progress);
        if (filled > 0) {
            uiSprite.drawFastHLine(MARGIN, PROG_Y, filled, uiAnimLerp(FG_4, CANVAS, progT));
        }
    }
    return b;
}

// ── API ──

// Arranca la transición y borra lo que el dibujado nuevo no vaya a tapar.
//
// Las bandas de los renglones se solapan entre sí (cada una llega 22 px más
// abajo que el arranque de la siguiente), así que entre renglón y renglón no
// quedan huecos. Lo único que puede quedar colgado es lo que estaba más abajo
// del último renglón nuevo, y el pie cuando se pasa a una pantalla de estado.
static void arrancar(int lineasNuevas, bool esStatus) {
    const bool completo = uiAnimBegin();

    if (!completo) {
        int top = SCREEN_H, bottom = 0;
        for (int i = lineasNuevas; i < s_prevLines; i++) {
            const int y0 = TEXT_Y + i * TEXT_LH - 4;
            const int y1 = TEXT_Y + i * TEXT_LH + TEXT_LH + 4;
            if (y0 < top) top = y0;
            if (y1 > bottom) bottom = y1;
        }
        // Una pantalla de estado no dibuja pie: el del ítem anterior sobra.
        if (esStatus && !s_prevStatus) {
            const int y0 = FOOT_Y - 4;
            const int y1 = FOOT_Y + FEED_IMG_SIDE + 8;
            if (y0 < top) top = y0;
            if (y1 > bottom) bottom = y1;
        }
        // Una sola franja para todo: dos empujes cuestan dos VSync y el sobrante
        // casi siempre es contiguo.
        if (bottom > top) uiAnimClearBand(top, bottom - top);
    }

    s_prevLines = lineasNuevas;
    s_prevStatus = esStatus;
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

    uiSprite.setFont(DS::fontHeading());
    s_cur.lineCount = wrapText(item->text, s_cur.lines, SCREEN_W - 2 * MARGIN);

    // La imagen se baja una sola vez acá, nunca dentro de un frame de
    // animación: un GET de 8 KB en medio de la transición la cortaría.
    s_cur.img = feedFetchImage(item->imgKey);

    arrancar(s_cur.lineCount, false);
}

void uiFercedShowStatus(const char* eyebrow, const char* message) {
    if (!s_ready) return;

    memset(&s_cur, 0, sizeof(s_cur));
    s_cur.isStatus = true;
    snprintf(s_cur.chip, sizeof(s_cur.chip), "%s", eyebrow ? eyebrow : "");
    for (char* c = s_cur.chip; *c; c++) *c = toupper((unsigned char)*c);

    uiSprite.setFont(DS::fontHeading());
    s_cur.lineCount = wrapText(message ? message : "", s_cur.lines,
                               SCREEN_W - 2 * MARGIN);
    arrancar(s_cur.lineCount, true);
}

bool uiFercedTick(uint32_t nowEpoch, float progress01) {
    if (!s_ready || !s_hasContent) return false;

    const uint32_t now = millis();
    s_progress = progress01 < 0.0f ? 0.0f : (progress01 > 1.0f ? 1.0f : progress01);

    if (s_animating) {
        const uint32_t elapsed = uiAnimElapsed();
        const uint32_t t0 = micros();

        relativeTime(s_cur.when, sizeof(s_cur.when), s_cur.epoch, nowEpoch);
        const UiBand b = paintFrame(elapsed, nowEpoch);
        uiAnimPresent(b.y, b.h);
        if (b.h > 0) uiAnimCountFrame(t0);

        if (elapsed >= uiAnimTotalMs(SLOTS)) {
            s_animating = false;
            s_lastProgressPaint = now;
            uiAnimPublishStats();
        }
        return true;
    }

    // En reposo repinta 4 veces por segundo: alcanza para que la línea de
    // progreso avance suave y no tiene costo perceptible.
    if (now - s_lastProgressPaint >= 250) {
        s_lastProgressPaint = now;
        relativeTime(s_cur.when, sizeof(s_cur.when), s_cur.epoch, nowEpoch);
        const UiBand b = paintFrame(0xFFFF, nowEpoch);
        uiAnimPresent(b.y, b.h);
    }
    return false;
}

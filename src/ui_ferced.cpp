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

// â”€â”€ GeometrÃ­a â”€â”€
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

// â”€â”€ AnimaciÃ³n â”€â”€
// DuraciÃ³n corta y stagger chico: la sensaciÃ³n de "caro" viene de que los
// elementos no entren todos juntos, no de que tarde.
// El stagger tiene que ser del orden de la duracion, no mucho menor: si los
// elementos se solapan, todos estan vivos a la vez, la banda sucia es la
// pantalla entera y el frame vuelve a costar 80 ms. Casi en secuencia, cada
// uno repinta ~70 px y el frame entra holgado en un VSync de 23,6 ms.
// STAGGER > ENTER: estrictamente en secuencia. Con solapamiento la banda sucia
// es la union de dos elementos (~180 px) y el frame cae en 2 VSync. Sin
// solapamiento es un elemento (~70 px) y entra en uno solo.
static constexpr uint16_t ENTER_MS   = 130;
static constexpr uint16_t STAGGER_MS = 140;
static constexpr int      RISE_PX    = 22;

// 0 = chip y marca, 1..5 = renglones de texto, 6 = pie, 7 = progreso.
static constexpr uint8_t  SLOT_CHIP  = 0;
static constexpr uint8_t  SLOT_LINE0 = 1;
static constexpr uint8_t  SLOT_FOOT  = 6;
static constexpr uint8_t  SLOT_PROG  = 7;
static constexpr uint8_t  SLOTS      = 8;

// easeOutQuint aproxima cubic-bezier(.16, 1, .3, 1), el easing de la marca:
// arranca rÃ¡pido y frena largo. Barato de calcular en un MCU.
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

// â”€â”€ Estado del Ã­tem en pantalla â”€â”€
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

// Medicion del frame: sin esto el framerate es una suposicion.
static uint32_t s_frameCount = 0;
static uint32_t s_frameSumUs = 0;
static uint32_t s_frameWorstUs = 0;
// El costo dentro del tick no es el framerate: afuera quedan el delay() del
// loop, el sondeo del tactil y wifiLoop(). Para no inflar el numero, se mide
// tambien el periodo real entre arranques de frame.
static uint32_t s_lastFrameUs = 0;
static uint32_t s_framePeriodSumUs = 0;
static uint32_t s_framePeriodCount = 0;
static UiFrameStats s_stats = {0, 0, 0};

UiFrameStats uiFercedStats() { return s_stats; }

void uiFercedSetup() {
    if (s_ready) return;
    s_sprite.setPsram(true);
    s_sprite.setColorDepth(16);
    s_sprite.createSprite(SCREEN_W, SCREEN_H);
    memset(&s_cur, 0, sizeof(s_cur));
    s_ready = true;
}

// â”€â”€ Primitivas â”€â”€

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

// Corta por ancho real en pÃ­xeles: contar caracteres con fuente proporcional
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

// Se recalcula en cada dibujado: aunque el Ã­tem se repita, el tiempo avanza y
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

// â”€â”€ ComposiciÃ³n â”€â”€

// Banda vertical que toca redibujar en este frame. Fuera de ella el sprite ya
// tiene el contenido bueno del frame anterior y no hace falta ni limpiar ni
// empujar.
struct Band { int y, h; };

static Band dirtyBand(uint32_t elapsed) {
    if (elapsed == 0xFFFF) {                 // repintado de reposo
        return Band{PROG_Y - 4, 10};         // solo la linea de progreso
    }
    const uint32_t total = (uint32_t)ENTER_MS + (uint32_t)STAGGER_MS * SLOTS;
    // Frame de cierre: todo lo anterior ya se empujo al llegar a su valor
    // final. Repintar la pantalla entera aca costaba 99 ms para nada.
    if (elapsed >= total) return Band{PROG_Y - 6, RISE_PX + 12};

    int top = SCREEN_H, bottom = 0;
    // Un elemento esta "vivo" mientras su t no llego a 1. Cada renglon tiene
    // banda propia: asi la zona a repintar es de ~70 px, no de 480.
    for (uint8_t slot = 0; slot < SLOTS; slot++) {
        const float t = slotT(elapsed, slot);
        if (t <= 0.0f || t >= 1.0f) continue;
        int y0, y1;
        if (slot == SLOT_CHIP) {
            y0 = CHIP_Y - 4;
            y1 = CHIP_Y + CHIP_H + RISE_PX;
        } else if (slot == SLOT_FOOT) {
            y0 = FOOT_Y - 4;
            y1 = FOOT_Y + FEED_IMG_SIDE + RISE_PX;
        } else if (slot == SLOT_PROG) {
            y0 = PROG_Y - 6;
            y1 = PROG_Y + RISE_PX + 6;
        } else {
            const int i = slot - SLOT_LINE0;
            y0 = TEXT_Y + i * TEXT_LH - 4;
            y1 = TEXT_Y + i * TEXT_LH + TEXT_LH + RISE_PX;
        }
        if (y0 < top) top = y0;
        if (y1 > bottom) bottom = y1;
    }
    // Sin ningun elemento vivo no hay nada que repintar. Devolver la pantalla
    // entera aca era una trampa: con los slots en secuencia quedan huecos de
    // 10 ms entre uno y otro, y cada hueco costaba un repintado de 99 ms.
    if (bottom <= top) return Band{0, 0};
    return Band{top, bottom - top};
}

// Dibujar algo fuera de la banda que se va a empujar es trabajo tirado, y en
// este panel el trabajo tirado se paga en frames perdidos.
static inline bool touches(const Band& b, int y0, int y1) {
    return y1 >= b.y && y0 <= b.y + b.h;
}

static Band paintFrame(uint32_t elapsed, uint32_t nowEpoch) {
    const Band b = dirtyBand(elapsed);
    if (b.h <= 0) return b;   // nada vivo: ni limpiar ni componer
    s_sprite.fillRect(0, b.y, SCREEN_W, b.h, CANVAS);

    const float markT = slotT(elapsed, SLOT_CHIP);
    const bool chipBand = touches(b, CHIP_Y - 4, CHIP_Y + CHIP_H + RISE_PX);
    if (chipBand) drawMark(SCREEN_W - MARGIN - MARK_W, CHIP_Y, markT);

    // Chip de fuente
    const float chipT = markT;
    if (chipBand && chipT > 0.0f && s_cur.chip[0]) {
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
        const int ly = TEXT_Y + i * TEXT_LH;
        if (!touches(b, ly - 4, ly + TEXT_LH + RISE_PX)) continue;
        const float t = slotT(elapsed, SLOT_LINE0 + i);
        if (t <= 0.0f) continue;
        const int dy = (int)((1.0f - t) * RISE_PX);
        s_sprite.setTextColor(lerp565(s_cur.isStatus ? FG_2 : FG, CANVAS, t));
        s_sprite.drawString(s_cur.lines[i], MARGIN, TEXT_Y + i * TEXT_LH + dy);
    }

    if (s_cur.isStatus) return b;

    // Pie: imagen, autor, tiempo
    const float footT = slotT(elapsed, SLOT_FOOT);
    if (footT > 0.0f && touches(b, FOOT_Y - 4, FOOT_Y + FEED_IMG_SIDE + RISE_PX)) {
        const int dy = (int)((1.0f - footT) * RISE_PX);
        int textX = MARGIN;

        if (s_cur.img) {
            // pushImage copia por filas; el fundido pixel a pixel costaba 4096
            // llamadas a drawPixel por frame. La imagen entra por movimiento,
            // que es igual de elegante y practicamente gratis.
            // El cast es necesario: con un uint16_t* pelado LovyanGFX asume
            // orden intercambiado (el de SPI) y la imagen sale con los colores
            // rotos. El proxy escribe RGB565 en orden nativo.
            s_sprite.pushImage(MARGIN, FOOT_Y + dy,
                               FEED_IMG_SIDE, FEED_IMG_SIDE,
                               (const lgfx::rgb565_t*)s_cur.img);
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

    // LÃ­nea de progreso hacia el prÃ³ximo Ã­tem: da vida constante sin ruido.
    const float progT = slotT(elapsed, SLOT_PROG);
    if (progT > 0.0f) {
        const int w = SCREEN_W - 2 * MARGIN;
        s_sprite.drawFastHLine(MARGIN, PROG_Y, w, lerp565(LINE, CANVAS, progT));
        const int filled = (int)(w * s_progress);
        if (filled > 0) {
            s_sprite.drawFastHLine(MARGIN, PROG_Y, filled, lerp565(FG_4, CANVAS, progT));
        }
    }
    return b;
}

// Empuja solo la banda indicada. El panel refresca a 42 Hz (12 MHz de pclk
// sobre 548x518 con porches), o sea 23,6 ms por VSync. Empujar los 460 KB
// enteros no entra en ese presupuesto: medido daba 98,8 ms por frame, 10 fps.
static void present(int y, int h) {
    if (h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (h <= 0) return;

    displayWaitVSync();
    const uint32_t t0 = micros();
    s_sprite.setClipRect(0, y, SCREEN_W, h);
    tft.setClipRect(0, y, SCREEN_W, h);
    s_sprite.pushSprite(0, 0);
    tft.clearClipRect();
    s_sprite.clearClipRect();
    displayRecordPush(SCREEN_W * h * 2, micros() - t0);
}

// â”€â”€ API â”€â”€

static void beginSlide() {
    // Un borrado completo al empezar cada item, y recien despues las bandas.
    //
    // Sin esto pasan dos cosas: los huecos entre bandas nunca reciben el color
    // de fondo y quedan en el negro puro con el que nace el sprite, y si el
    // item nuevo tiene menos renglones que el anterior, los de mas abajo
    // quedan en pantalla porque su banda ya no se anima.
    //
    // Cuesta un frame caro (~99 ms) por item, o sea uno cada 17 s. A cambio,
    // los ~30 frames de la animacion siguen siendo baratos.
    s_sprite.fillScreen(CANVAS);
    displayWaitVSync();
    s_sprite.pushSprite(0, 0);
    displayRecordPush(SCREEN_W * SCREEN_H * 2, 0);

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

    // La imagen se baja una sola vez acÃ¡, nunca dentro de un frame de
    // animaciÃ³n: un GET de 8 KB en medio de la transiciÃ³n la cortarÃ­a.
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
        const uint32_t t0 = micros();

        relativeTime(s_cur.when, sizeof(s_cur.when), s_cur.epoch, nowEpoch);
        const Band b = paintFrame(elapsed, nowEpoch);
        present(b.y, b.h);

        // Los frames sin banda no dibujan nada: contarlos falsearia la media
        // hacia abajo y taparia el costo real de los que si pintan.
        if (b.h > 0) {
            const uint32_t frameUs = micros() - t0;
            s_frameCount++;
            s_frameSumUs += frameUs;
            if (frameUs > s_frameWorstUs) s_frameWorstUs = frameUs;
            if (s_lastFrameUs != 0) {
                s_framePeriodSumUs += t0 - s_lastFrameUs;
                s_framePeriodCount++;
            }
            s_lastFrameUs = t0;
        }

        if (elapsed >= (uint32_t)ENTER_MS + (uint32_t)STAGGER_MS * SLOTS) {
            s_animating = false;
            s_lastProgressPaint = now;
            if (s_frameCount > 0) {
                s_stats.frames = (uint16_t)s_frameCount;
                s_stats.avgUs100 = (uint16_t)((s_frameSumUs / s_frameCount) / 100);
                s_stats.worstUs100 = (uint16_t)(s_frameWorstUs / 100);

                // El mismo dato que viaja al proxy como [anim], pero por serie:
                // medir no puede depender de leer un log remoto. waitTimeouts
                // delata si displayWaitVSync() esta agotando su timeout en vez
                // de sincronizar de verdad con el panel.
                const DisplayDiagnostics d = displayGetDiagnostics();
                const float costMs = (float)s_frameSumUs / (float)s_frameCount / 1000.0f;
                const float periodMs = s_framePeriodCount > 0
                    ? (float)s_framePeriodSumUs / (float)s_framePeriodCount / 1000.0f
                    : 0.0f;
                Serial.printf(
                    "[anim] frames=%lu cost=%.1fms max=%.1fms period=%.1fms "
                    "fps=%.1f waits=%lu timeouts=%lu\n",
                    (unsigned long)s_frameCount, costMs,
                    (float)s_frameWorstUs / 1000.0f, periodMs,
                    periodMs > 0.0f ? 1000.0f / periodMs : 0.0f,
                    (unsigned long)d.waitCalls, (unsigned long)d.waitTimeouts);
            }
            s_frameCount = 0;
            s_frameSumUs = 0;
            s_frameWorstUs = 0;
            s_lastFrameUs = 0;
            s_framePeriodSumUs = 0;
            s_framePeriodCount = 0;
        }
        return true;
    }

    // En reposo repinta 4 veces por segundo: alcanza para que la lÃ­nea de
    // progreso avance suave y no tiene costo perceptible.
    if (now - s_lastProgressPaint >= 250) {
        s_lastProgressPaint = now;
        relativeTime(s_cur.when, sizeof(s_cur.when), s_cur.epoch, nowEpoch);
        const Band b = paintFrame(0xFFFF, nowEpoch);
        present(b.y, b.h);
    }
    return false;
}



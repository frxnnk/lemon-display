#include "ui_ferced.h"
#include "config.h"
#include "design_system.h"
#include "ui_chrome.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;

// El sprite, el escalonado, la espera de VSync y la medicion viven en
// ui_anim.cpp: los comparten esta pantalla, la de padel y las estaticas.

static bool s_ready = false;

// ── Geometría ──
//
// La pantalla es una pagina: cabecera con la firma, regla, titular grande y el
// riel de la marca al pie. Antes el titular iba en 12 pt y quedaba un hueco
// muerto de 116 px entre el texto y el pie; el titular era el unico contenido
// de la pantalla y se leia como un parrafo.

static constexpr int MARGIN  = FercedChrome::MARGIN;
static constexpr int MARK_W  = 11;

static constexpr int HEAD_Y  = 28;                 // arriba de la miniatura
static constexpr int THUMB   = FEED_IMG_SIDE;      // 64
static constexpr int RULE_Y  = 112;

static constexpr int TEXT_Y   = 150;
static constexpr int TEXT_LH  = 46;
static constexpr int MAX_LINES = 5;

static constexpr int RAIL_Y  = FercedChrome::RAIL_Y;

// 0 = cabecera, 1..5 = los cinco renglones de la grilla, 6 = riel.
static constexpr uint8_t SLOT_HEAD  = 0;
static constexpr uint8_t SLOT_LINE0 = 1;
static constexpr uint8_t SLOT_RAIL  = 6;
static constexpr uint8_t SLOTS      = 7;

// Las bandas de los slots TESELAN la pantalla entera: la del slot n arranca
// antes de que termine la del n-1 y entre todas cubren de 0 a 480. Eso es lo
// que permite que una app entre encima de otra sin limpiar nada primero —la ola
// tapa todo lo viejo a su paso— y lo que hizo innecesaria la contabilidad de
// renglones huerfanos que habia antes: un renglon sin contenido no se saltea,
// se limpia en su turno.
//
// Teselar no es gratis. Los huecos que antes no repintaba nadie —el aire sobre
// la cabecera y el que queda bajo el ultimo renglon— ahora se los reparten los
// slots de los extremos, que pasan de ~64 filas a 126 y 84. Con el modelo
// medido (23,4 ms + 0,1575 por fila) el peor frame sube de 37,6 a 43,2 ms: son
// cuatro frames de los ~40 de una transicion. Se paga eso a cambio de que
// cambiar de app deje de costar un borrado de 99 ms con el panel en negro.
//
// El reparto esta balanceado a proposito: cargarle todo el hueco a la cabecera
// daba 140 filas y 45,4 ms.
static constexpr int HEAD_FIN = 126;
static constexpr int RAIL_INI = 396;

static void bandaDe(uint8_t slot, int& y0, int& y1) {
    if (slot == SLOT_HEAD) { y0 = 0; y1 = HEAD_FIN; return; }
    if (slot == SLOT_RAIL) { y0 = RAIL_INI; y1 = SCREEN_H; return; }
    const int i = slot - SLOT_LINE0;
    y0 = (i == 0) ? HEAD_FIN - 4 : TEXT_Y + i * TEXT_LH - 4;
    y1 = TEXT_Y + i * TEXT_LH + TEXT_LH + UI_RISE_PX;
}

// ── Estado del ítem en pantalla ──
struct Slide {
    char     lines[MAX_LINES][64];
    int      lineCount;
    char     fuente[48];
    char     when[32];
    uint32_t epoch;
    const uint16_t* img;
    bool     offline;
    bool     isStatus;
};

static Slide    s_cur;
static bool     s_animating = false;
static bool     s_hasContent = false;
static float    s_progress = 0.0f;
static uint32_t s_lastProgressPaint = 0;

void uiFercedSetup() {
    if (s_ready) return;
    uiAnimSetup();
    memset(&s_cur, 0, sizeof(s_cur));
    s_ready = true;
}

// ── Primitivas ──

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

// El bloque de texto se cuelga de la grilla de cinco renglones, corrido lo
// necesario para quedar centrado. Como el corrimiento es en pasos de un
// renglon entero, cada linea sigue cayendo en una fila de la grilla y las
// bandas del escalonado no se mueven: un titular de dos renglones y uno de
// cinco se animan igual.
static int offsetGrilla(int lineCount) {
    if (lineCount >= MAX_LINES) return 0;
    return (MAX_LINES - lineCount) / 2;
}

// Se recalcula en cada dibujado: aunque el ítem se repita, el tiempo avanza y
// la pantalla no se siente congelada.
static void relativeTime(char* out, size_t len, uint32_t epoch, uint32_t now) {
    if (epoch == 0 || now == 0 || now < epoch) { snprintf(out, len, "recién"); return; }
    const uint32_t d = now - epoch;
    if (d < 60)    { snprintf(out, len, "recién"); return; }
    if (d < 3600)  { snprintf(out, len, "hace %lu min", (unsigned long)(d / 60)); return; }
    if (d < 86400) { snprintf(out, len, "hace %lu h", (unsigned long)(d / 3600)); return; }
    snprintf(out, len, "hace %lu d", (unsigned long)(d / 86400));
}

static const char* sourceLabel(const FeedItem* it) {
    if (it->origin == FEED_FROM_X) return it->handle[0] ? it->handle : "X";
    if (it->author[0]) return it->author;
    return "Noticias";
}

// ── Composición ──

static UiBand dirtyBand(uint32_t elapsed) {
    if (elapsed == 0xFFFF) {                 // repintado de reposo
        return UiBand{RAIL_Y - 2, FercedChrome::RAIL_H + 4};
    }
    const uint32_t total = uiAnimTotalMs(SLOTS);
    // Frame de cierre: todo lo anterior ya se empujo al llegar a su valor
    // final. Repintar la pantalla entera aca costaba 99 ms para nada.
    if (elapsed >= total) return UiBand{RAIL_Y - 2, FercedChrome::RAIL_H + 4};

    int top = SCREEN_H, bottom = 0;
    for (uint8_t slot = 0; slot < SLOTS; slot++) {
        const float t = uiAnimSlotT(elapsed, slot);
        if (t <= 0.0f || t >= 1.0f) continue;
        int y0, y1;
        bandaDe(slot, y0, y1);
        if (y0 < top) top = y0;
        if (y1 > bottom) bottom = y1;
    }
    // Sin ningun elemento vivo no hay nada que repintar. Devolver la pantalla
    // entera aca era una trampa: con los slots en secuencia quedan huecos de
    // 10 ms entre uno y otro, y cada hueco costaba un repintado de 99 ms.
    if (bottom <= top) return UiBand{0, 0};
    return UiBand{top, bottom - top};
}

// Cabecera: la miniatura como sello de la fuente, el nombre y —en la cursiva
// con gracias de la marca— cuanto hace. Es la firma del titular, no un pie.
static void pintarCabecera(float t) {
    const int dy = (int)((1.0f - t) * UI_RISE_PX);
    int textX = MARGIN;

    if (!s_cur.isStatus) {
        if (s_cur.img) {
            // El cast es necesario: con un uint16_t* pelado LovyanGFX asume
            // orden intercambiado (el de SPI) y la imagen sale con los colores
            // rotos. El proxy escribe RGB565 en orden nativo.
            uiSprite.pushImage(MARGIN, HEAD_Y + dy, THUMB, THUMB,
                               (const lgfx::rgb565_t*)s_cur.img);
            uiRoundCorners(uiSprite, MARGIN, HEAD_Y + dy, THUMB, THUMB, 10, CANVAS);
        } else {
            // Sin imagen, la inicial de la fuente ocupa el mismo cuadrado. La
            // alternativa era correr el texto al margen, pero entonces la
            // cabecera cambiaba de forma segun el item y media pantalla quedaba
            // vacia: la mitad de los titulares del feed no traen miniatura.
            uiSprite.fillSmoothRoundRect(MARGIN, HEAD_Y + dy, THUMB, THUMB, 10,
                                         uiAnimLerp(SURFACE, CANVAS, t));
            const char inicial[2] = {s_cur.fuente[0], '\0'};
            uiSprite.setFont(DS::fontTitular());
            uiSprite.setTextDatum(lgfx::middle_center);
            uiSprite.setTextColor(uiAnimLerp(FG_4, CANVAS, t));
            uiSprite.drawString(inicial, MARGIN + THUMB / 2, HEAD_Y + THUMB / 2 + dy);
        }
        textX += THUMB + 20;
    }

    const int centro = HEAD_Y + THUMB / 2 + dy;
    uiSprite.setTextDatum(lgfx::middle_left);

    if (s_cur.fuente[0]) {
        uiSprite.setFont(DS::fontHeading());
        uiSprite.setTextColor(uiAnimLerp(s_cur.isStatus ? FG_3 : FG_2, CANVAS, t));
        uiSprite.drawString(s_cur.fuente, textX, centro - 14);
    }
    if (s_cur.when[0]) {
        uiSprite.setFont(DS::fontAcentoSm());
        uiSprite.setTextColor(uiAnimLerp(FG_3, CANVAS, t));
        uiSprite.drawString(s_cur.when, textX, centro + 14);
    }

    // Sin conexion: el punto rojo es de los pocos usos legitimos del acento.
    if (s_cur.offline) {
        uiSprite.fillCircle(SCREEN_W - MARGIN - 5, HEAD_Y + THUMB - 2 + dy, 4,
                            uiAnimLerp(DANGER, CANVAS, t));
    }

    uiMark(uiSprite, SCREEN_W - MARGIN - MARK_W, HEAD_Y + 2 + dy, t);
    // La regla aparece pero no sube. Subiendo 22 px como el resto terminaba en
    // 134, fuera de la banda del slot, y se dibujaba recortada durante toda la
    // entrada. Y ademas: es el eje de la composicion, no un elemento que llega.
    uiSprite.drawFastHLine(MARGIN, RULE_Y, SCREEN_W - 2 * MARGIN,
                           uiAnimLerp(LINE, CANVAS, t));
}

static UiBand paintFrame(uint32_t elapsed) {
    const UiBand b = dirtyBand(elapsed);
    if (b.h <= 0) return b;   // nada vivo: ni limpiar ni componer
    uiSprite.fillRect(0, b.y, SCREEN_W, b.h, CANVAS);

    int y0, y1;
    bandaDe(SLOT_HEAD, y0, y1);
    if (uiAnimTouches(b, y0, y1)) {
        const float t = uiAnimSlotT(elapsed, SLOT_HEAD);
        if (t > 0.0f) pintarCabecera(t);
    }

    // Texto: los renglones entran de a uno, no en bloque. Se recorre la grilla
    // completa y no solo las lineas con contenido: asi la fila que quedo vacia
    // porque el titular nuevo es mas corto se limpia en su turno, dentro de la
    // ola, en vez de necesitar un borrado aparte.
    const int off = offsetGrilla(s_cur.lineCount);
    uiSprite.setFont(DS::fontTitular());
    uiSprite.setTextDatum(lgfx::top_left);
    for (int r = 0; r < MAX_LINES; r++) {
        bandaDe(SLOT_LINE0 + r, y0, y1);
        if (!uiAnimTouches(b, y0, y1)) continue;
        const int i = r - off;
        if (i < 0 || i >= s_cur.lineCount) continue;
        const float t = uiAnimSlotT(elapsed, SLOT_LINE0 + r);
        if (t <= 0.0f) continue;
        const int dy = (int)((1.0f - t) * UI_RISE_PX);
        uiSprite.setTextColor(uiAnimLerp(s_cur.isStatus ? FG_2 : FG, CANVAS, t));
        uiSprite.drawString(s_cur.lines[i], MARGIN, TEXT_Y + r * TEXT_LH + dy);
    }

    // El riel de la marca: cuanto falta para el proximo titular. Es lo unico
    // con color de todo el sistema y esta siempre en el mismo renglon.
    const float railT = uiAnimSlotT(elapsed, SLOT_RAIL);
    if (railT > 0.0f) uiRail(uiSprite, RAIL_Y, s_progress, railT);

    return b;
}

// ── API ──

void uiFercedShowItem(const FeedItem* item, uint8_t index, uint8_t total,
                      bool offline) {
    (void)index; (void)total;
    if (!s_ready || !item) return;

    memset(&s_cur, 0, sizeof(s_cur));
    s_cur.isStatus = false;
    s_cur.offline = offline;
    s_cur.epoch = item->epoch;

    snprintf(s_cur.fuente, sizeof(s_cur.fuente), "%s", sourceLabel(item));

    uiSprite.setFont(DS::fontTitular());
    s_cur.lineCount = wrapText(item->text, s_cur.lines, SCREEN_W - 2 * MARGIN);

    // La imagen se baja una sola vez acá, nunca dentro de un frame de
    // animación: un GET de 8 KB en medio de la transición la cortaría.
    s_cur.img = feedFetchImage(item->imgKey);

    uiAnimBegin();
    s_animating = true;
    s_hasContent = true;
}

void uiFercedShowStatus(const char* eyebrow, const char* message) {
    if (!s_ready) return;

    memset(&s_cur, 0, sizeof(s_cur));
    s_cur.isStatus = true;
    snprintf(s_cur.fuente, sizeof(s_cur.fuente), "%s", eyebrow ? eyebrow : "");

    uiSprite.setFont(DS::fontTitular());
    s_cur.lineCount = wrapText(message ? message : "", s_cur.lines,
                               SCREEN_W - 2 * MARGIN);

    uiAnimBegin();
    s_animating = true;
    s_hasContent = true;
}

bool uiFercedTick(uint32_t nowEpoch, float progress01) {
    if (!s_ready || !s_hasContent) return false;

    const uint32_t now = millis();
    s_progress = progress01 < 0.0f ? 0.0f : (progress01 > 1.0f ? 1.0f : progress01);

    if (s_animating) {
        const uint32_t elapsed = uiAnimElapsed();
        const uint32_t t0 = micros();

        if (!s_cur.isStatus) {
            relativeTime(s_cur.when, sizeof(s_cur.when), s_cur.epoch, nowEpoch);
        }
        const UiBand b = paintFrame(elapsed);
        uiAnimPresent(b.y, b.h);
        if (b.h > 0) uiAnimCountFrame(t0);

        if (elapsed >= uiAnimTotalMs(SLOTS)) {
            s_animating = false;
            s_lastProgressPaint = now;
            uiAnimPublishStats();
        }
        return true;
    }

    // En reposo repinta 4 veces por segundo: alcanza para que el riel avance
    // suave y no tiene costo perceptible — son dos renglones, no la pantalla.
    if (now - s_lastProgressPaint >= 250) {
        s_lastProgressPaint = now;
        const UiBand b = paintFrame(0xFFFF);
        uiAnimPresent(b.y, b.h);
    }
    return false;
}

#include "ui_padel.h"
#include "config.h"
#include "design_system.h"
#include "data/ferced_mark_11.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;

// ── Geometría ──
// El eje de la pantalla es el enfrentamiento: las dos parejas son lo único en
// tipografía de título, y todo lo demás —hora, cancha, fase, sede— baja a
// texto de apoyo. Es la misma jerarquía que en noticias, donde el titular manda
// y la fuente y la hora quedan en el pie.
static constexpr int MARGIN = 28;
static constexpr int MARK_W = 11;
static constexpr int MARK_H = 28;
static constexpr int CHIP_Y = 30;
static constexpr int CHIP_H = 28;

static constexpr int CAP_Y   = 98;    // "10:00 · CENTER COURT"
static constexpr int CAP_Y2  = 124;   // "R32 · FEMENINO"
static constexpr int PA_Y    = 172;   // pareja A
static constexpr int PA_Y2   = 208;
static constexpr int RULE_Y  = 250;
static constexpr int PB_Y    = 282;   // pareja B
static constexpr int PB_Y2   = 318;
static constexpr int FOOT_Y  = 396;
static constexpr int PROG_Y  = 452;

// Modo torneo: el nombre es el héroe y la cuenta regresiva el dato duro.
static constexpr int T_NAME_Y = 140;
static constexpr int T_NAME_Y2 = 178;
static constexpr int T_BIG_Y  = 244;
static constexpr int T_SUB_Y  = 306;
static constexpr int T_SUB_Y2 = 332;

static constexpr uint8_t SLOTS = 7;

enum Modo : uint8_t { MODO_PARTIDO, MODO_TORNEO, MODO_ESTADO };

// Banda vertical de cada slot. Se llena al preparar la pantalla, así el cálculo
// de la zona sucia no tiene que saber en qué modo está.
struct Row {
    int16_t y0, y1;
};

static Row      s_rows[SLOTS];
static Modo     s_modo = MODO_ESTADO;
static bool     s_ready = false;
static bool     s_animating = false;
static bool     s_hasContent = false;
static float    s_progress = 0.0f;
static uint32_t s_lastProgressPaint = 0;

// Contenido ya resuelto: la pantalla no vuelve a consultar el cliente ni a
// formatear nada dentro de un frame de animación.
static struct {
    char chip[40];
    char cap1[52];
    char cap2[40];
    char a1[PADEL_NAME_LEN], a2[PADEL_NAME_LEN];
    char b1[PADEL_NAME_LEN], b2[PADEL_NAME_LEN];
    char seedA[8], seedB[8];
    char scoreA[16], scoreB[16];
    char pie[56];
    char name1[40], name2[40];   // modo torneo: nombre en hasta dos renglones
    char big[24];                // "en 32 días" / "día 4 de 8"
    char sub1[28], sub2[44];
    uint8_t ganador;             // 0 sin definir, 1 pareja A, 2 pareja B
    bool jugando;
} s_cur;

void uiPadelSetup() {
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

// Corta en dos renglones por ancho real. Los nombres de torneo van de "PARIS
// MAJOR" a "COMUNIDAD DE MADRID P1": contar caracteres dejaría cortes feos.
static void wrap2(const char* text, char* l1, size_t n1, char* l2, size_t n2, int maxW) {
    l1[0] = l2[0] = '\0';
    if (!text || !text[0]) return;

    if (uiSprite.textWidth(text) <= maxW) {
        snprintf(l1, n1, "%s", text);
        return;
    }
    // Se corta en el último espacio que todavía entra.
    int corte = 0;
    char buf[80];
    for (int i = 0; text[i]; i++) {
        if (text[i] != ' ') continue;
        snprintf(buf, sizeof(buf), "%.*s", i, text);
        if (uiSprite.textWidth(buf) > maxW) break;
        corte = i;
    }
    if (corte == 0) {   // una sola palabra larguísima: entra lo que entre
        snprintf(l1, n1, "%s", text);
        return;
    }
    snprintf(l1, n1, "%.*s", corte, text);
    snprintf(l2, n2, "%s", text + corte + 1);
}

// Cuenta sets ganados para saber quién ganó. Sin esto habría que pintar los dos
// resultados iguales y la pantalla no diría lo único que importa de un partido
// terminado.
static uint8_t ganador(const char* a, const char* b) {
    if (!a[0] || !b[0]) return 0;
    int ga = 0, gb = 0;
    const char *pa = a, *pb = b;
    while (*pa && *pb) {
        const int va = atoi(pa), vb = atoi(pb);
        if (va > vb) ga++;
        else if (vb > va) gb++;
        while (*pa && *pa != ' ') pa++;
        while (*pa == ' ') pa++;
        while (*pb && *pb != ' ') pb++;
        while (*pb == ' ') pb++;
    }
    if (ga > gb) return 1;
    if (gb > ga) return 2;
    return 0;
}

static void filas(Modo m) {
    for (uint8_t i = 0; i < SLOTS; i++) s_rows[i] = {0, 0};
    s_rows[0] = {CHIP_Y - 4, (int16_t)(CHIP_Y + CHIP_H + UI_RISE_PX)};
    s_rows[6] = {PROG_Y - 6, (int16_t)(PROG_Y + UI_RISE_PX + 6)};

    if (m == MODO_PARTIDO) {
        s_rows[1] = {CAP_Y - 4, (int16_t)(CAP_Y2 + 26 + UI_RISE_PX)};
        s_rows[2] = {PA_Y - 4, (int16_t)(PA_Y2 + 30 + UI_RISE_PX)};
        s_rows[3] = {RULE_Y - 6, (int16_t)(RULE_Y + UI_RISE_PX + 6)};
        s_rows[4] = {PB_Y - 4, (int16_t)(PB_Y2 + 30 + UI_RISE_PX)};
        s_rows[5] = {FOOT_Y - 4, (int16_t)(FOOT_Y + 26 + UI_RISE_PX)};
    } else {
        s_rows[1] = {T_NAME_Y - 4, (int16_t)(T_NAME_Y + 32 + UI_RISE_PX)};
        s_rows[2] = {T_NAME_Y2 - 4, (int16_t)(T_NAME_Y2 + 32 + UI_RISE_PX)};
        s_rows[3] = {T_BIG_Y - 6, (int16_t)(T_BIG_Y + 40 + UI_RISE_PX)};
        s_rows[4] = {T_SUB_Y - 4, (int16_t)(T_SUB_Y + 26 + UI_RISE_PX)};
        s_rows[5] = {T_SUB_Y2 - 4, (int16_t)(T_SUB_Y2 + 26 + UI_RISE_PX)};
    }
}

// ── Composición ──

static UiBand dirtyBand(uint32_t elapsed) {
    if (elapsed == 0xFFFF) return UiBand{PROG_Y - 4, 10};
    if (elapsed >= uiAnimTotalMs(SLOTS)) return UiBand{PROG_Y - 6, UI_RISE_PX + 12};

    int top = SCREEN_H, bottom = 0;
    for (uint8_t slot = 0; slot < SLOTS; slot++) {
        if (s_rows[slot].y1 <= s_rows[slot].y0) continue;
        const float t = uiAnimSlotT(elapsed, slot);
        if (t <= 0.0f || t >= 1.0f) continue;
        if (s_rows[slot].y0 < top) top = s_rows[slot].y0;
        if (s_rows[slot].y1 > bottom) bottom = s_rows[slot].y1;
    }
    if (bottom <= top) return UiBand{0, 0};
    return UiBand{top, bottom - top};
}

static void pintarChip(const UiBand& b, float t) {
    if (!uiAnimTouches(b, s_rows[0].y0, s_rows[0].y1)) return;
    drawMark(SCREEN_W - MARGIN - MARK_W, CHIP_Y, t);
    if (t <= 0.0f || !s_cur.chip[0]) return;

    const int dy = (int)((1.0f - t) * UI_RISE_PX);
    uiSprite.setFont(DS::fontCaption());
    const int w = uiSprite.textWidth(s_cur.chip) + 26;
    uiSprite.fillSmoothRoundRect(MARGIN, CHIP_Y + dy, w, CHIP_H, CHIP_H / 2,
                                 uiAnimLerp(SURFACE, CANVAS, t));
    uiSprite.setTextDatum(lgfx::middle_left);
    uiSprite.setTextColor(uiAnimLerp(FG_3, CANVAS, t));
    uiSprite.drawString(s_cur.chip, MARGIN + 13, CHIP_Y + dy + CHIP_H / 2);
}

// Una pareja: dos nombres en tipografía de título, el número de cabeza de serie
// arriba a la derecha y el resultado abajo. El perdedor de un partido terminado
// baja a FG_3: es la forma más barata de que se lea quién ganó.
static void pintarPareja(const UiBand& b, uint8_t slot, int y1, int y2,
                         const char* n1, const char* n2,
                         const char* seed, const char* score, bool perdedor) {
    if (!uiAnimTouches(b, s_rows[slot].y0, s_rows[slot].y1)) return;
    const float t = uiAnimSlotT(uiAnimElapsed(), slot);
    if (t <= 0.0f) return;
    const int dy = (int)((1.0f - t) * UI_RISE_PX);
    const uint16_t base = perdedor ? FG_3 : FG;

    uiSprite.setFont(DS::fontHeading());
    uiSprite.setTextDatum(lgfx::top_left);
    uiSprite.setTextColor(uiAnimLerp(base, CANVAS, t));
    uiSprite.drawString(n1, MARGIN, y1 + dy);
    if (n2 && n2[0]) uiSprite.drawString(n2, MARGIN, y2 + dy);

    uiSprite.setTextDatum(lgfx::top_right);
    if (seed && seed[0]) {
        uiSprite.setFont(DS::fontCaption());
        uiSprite.setTextColor(uiAnimLerp(FG_4, CANVAS, t));
        uiSprite.drawString(seed, SCREEN_W - MARGIN, y1 + dy + 6);
    }
    if (score && score[0]) {
        uiSprite.setFont(DS::fontHeading());
        uiSprite.setTextColor(uiAnimLerp(base, CANVAS, t));
        uiSprite.drawString(score, SCREEN_W - MARGIN, y2 + dy);
    }
}

static void pintarLinea(const UiBand& b, uint8_t slot, int y, const char* txt,
                        const lgfx::IFont* font, uint16_t color) {
    if (!txt || !txt[0]) return;
    if (!uiAnimTouches(b, s_rows[slot].y0, s_rows[slot].y1)) return;
    const float t = uiAnimSlotT(uiAnimElapsed(), slot);
    if (t <= 0.0f) return;
    const int dy = (int)((1.0f - t) * UI_RISE_PX);
    uiSprite.setFont(font);
    uiSprite.setTextDatum(lgfx::top_left);
    uiSprite.setTextColor(uiAnimLerp(color, CANVAS, t));
    uiSprite.drawString(txt, MARGIN, y + dy);
}

static UiBand paintFrame(uint32_t elapsed) {
    const UiBand b = dirtyBand(elapsed);
    if (b.h <= 0) return b;
    uiSprite.fillRect(0, b.y, SCREEN_W, b.h, CANVAS);

    pintarChip(b, uiAnimSlotT(elapsed, 0));

    if (s_modo == MODO_PARTIDO) {
        // Las dos líneas de apoyo comparten slot: son un solo bloque de lectura.
        if (uiAnimTouches(b, s_rows[1].y0, s_rows[1].y1)) {
            const float t = uiAnimSlotT(elapsed, 1);
            if (t > 0.0f) {
                const int dy = (int)((1.0f - t) * UI_RISE_PX);
                uiSprite.setFont(DS::fontBody());
                uiSprite.setTextDatum(lgfx::top_left);
                uiSprite.setTextColor(uiAnimLerp(s_cur.jugando ? SUCCESS : FG_2, CANVAS, t));
                uiSprite.drawString(s_cur.cap1, MARGIN, CAP_Y + dy);
                uiSprite.setTextColor(uiAnimLerp(FG_3, CANVAS, t));
                uiSprite.drawString(s_cur.cap2, MARGIN, CAP_Y2 + dy);
            }
        }

        pintarPareja(b, 2, PA_Y, PA_Y2, s_cur.a1, s_cur.a2,
                     s_cur.seedA, s_cur.scoreA, s_cur.ganador == 2);

        if (uiAnimTouches(b, s_rows[3].y0, s_rows[3].y1)) {
            const float t = uiAnimSlotT(elapsed, 3);
            if (t > 0.0f) {
                const int dy = (int)((1.0f - t) * UI_RISE_PX);
                uiSprite.drawFastHLine(MARGIN, RULE_Y + dy, 64, uiAnimLerp(LINE, CANVAS, t));
            }
        }

        pintarPareja(b, 4, PB_Y, PB_Y2, s_cur.b1, s_cur.b2,
                     s_cur.seedB, s_cur.scoreB, s_cur.ganador == 1);

        pintarLinea(b, 5, FOOT_Y, s_cur.pie, DS::fontBody(), FG_3);
    } else {
        pintarLinea(b, 1, T_NAME_Y, s_cur.name1, DS::fontHeading(), FG);
        pintarLinea(b, 2, T_NAME_Y2, s_cur.name2, DS::fontHeading(), FG);
        pintarLinea(b, 3, T_BIG_Y, s_cur.big, DS::fontDataLg(),
                    s_modo == MODO_ESTADO ? FG_2 : FG);
        pintarLinea(b, 4, T_SUB_Y, s_cur.sub1, DS::fontBody(), FG_2);
        pintarLinea(b, 5, T_SUB_Y2, s_cur.sub2, DS::fontBody(), FG_3);
    }

    const float progT = uiAnimSlotT(elapsed, 6);
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

static void arrancar(Modo m) {
    s_modo = m;
    filas(m);
    uiAnimBegin();
    s_animating = true;
    s_hasContent = true;
}

static void mayusculas(char* s) {
    for (; *s; s++) *s = toupper((unsigned char)*s);
}

// El nombre del torneo casi siempre termina en su categoría ("LONDON P1",
// "PARIS MAJOR"), así que anteponerla daba "P1 LONDON P1". Sólo se agrega
// cuando aporta algo.
static void chipTorneo(char* out, size_t n, const char* prefijo,
                       const char* cat, const char* nombre) {
    const bool redundante = cat && cat[0] && nombre && strstr(nombre, cat) != nullptr;
    if (prefijo && prefijo[0]) {
        if (redundante || !cat || !cat[0]) snprintf(out, n, "%s", prefijo);
        else                               snprintf(out, n, "%s  ·  %s", prefijo, cat);
        return;
    }
    if (redundante || !cat || !cat[0]) snprintf(out, n, "%s", nombre ? nombre : "");
    else                               snprintf(out, n, "%s  ·  %s", cat, nombre);
}

static void mostrarPartido(const PadelMatch* m, const PadelTour* live) {
    memset(&s_cur, 0, sizeof(s_cur));

    if (live) chipTorneo(s_cur.chip, sizeof(s_cur.chip), nullptr, live->cat, live->name);
    else      snprintf(s_cur.chip, sizeof(s_cur.chip), "PÁDEL");
    mayusculas(s_cur.chip);

    s_cur.jugando = m->state == 1;
    const char* hora = m->time[0] ? m->time : "sin horario";
    if (m->court[0]) snprintf(s_cur.cap1, sizeof(s_cur.cap1), "%s  ·  %s", hora, m->court);
    else             snprintf(s_cur.cap1, sizeof(s_cur.cap1), "%s", hora);
    if (s_cur.jugando) snprintf(s_cur.cap1, sizeof(s_cur.cap1), "EN JUEGO  ·  %s", m->court);

    const char* gen = m->gender == 'F' ? "Femenino" : (m->gender == 'M' ? "Masculino" : "");
    if (m->round[0] && gen[0]) snprintf(s_cur.cap2, sizeof(s_cur.cap2), "%s  ·  %s", m->round, gen);
    else if (m->round[0])      snprintf(s_cur.cap2, sizeof(s_cur.cap2), "%s", m->round);
    else                       snprintf(s_cur.cap2, sizeof(s_cur.cap2), "%s", gen);

    snprintf(s_cur.a1, sizeof(s_cur.a1), "%s", m->a1);
    snprintf(s_cur.a2, sizeof(s_cur.a2), "%s", m->a2);
    snprintf(s_cur.b1, sizeof(s_cur.b1), "%s", m->b1);
    snprintf(s_cur.b2, sizeof(s_cur.b2), "%s", m->b2);
    if (m->seedA[0]) snprintf(s_cur.seedA, sizeof(s_cur.seedA), "(%s)", m->seedA);
    if (m->seedB[0]) snprintf(s_cur.seedB, sizeof(s_cur.seedB), "(%s)", m->seedB);
    snprintf(s_cur.scoreA, sizeof(s_cur.scoreA), "%s", m->scoreA);
    snprintf(s_cur.scoreB, sizeof(s_cur.scoreB), "%s", m->scoreB);
    if (m->state == 2) s_cur.ganador = ganador(m->scoreA, m->scoreB);

    if (live && live->days > 0) {
        snprintf(s_cur.pie, sizeof(s_cur.pie), "día %u de %u  ·  %s",
                 live->day, live->days, live->city);
    } else if (live) {
        snprintf(s_cur.pie, sizeof(s_cur.pie), "%s", live->city);
    }

    arrancar(MODO_PARTIDO);
}

static void mostrarTorneo(const PadelTour* t) {
    memset(&s_cur, 0, sizeof(s_cur));

    chipTorneo(s_cur.chip, sizeof(s_cur.chip),
               t->live ? "EN JUEGO" : "PRÓXIMO", t->cat, t->name);
    mayusculas(s_cur.chip);

    uiSprite.setFont(DS::fontHeading());
    wrap2(t->name, s_cur.name1, sizeof(s_cur.name1), s_cur.name2, sizeof(s_cur.name2),
          SCREEN_W - 2 * MARGIN);

    if (t->live && t->days > 0) {
        snprintf(s_cur.big, sizeof(s_cur.big), "día %u de %u", t->day, t->days);
    } else if (t->live) {
        snprintf(s_cur.big, sizeof(s_cur.big), "en juego");
    } else if (t->faltan == 0) {
        snprintf(s_cur.big, sizeof(s_cur.big), "arranca hoy");
    } else if (t->faltan == 1) {
        snprintf(s_cur.big, sizeof(s_cur.big), "mañana");
    } else {
        snprintf(s_cur.big, sizeof(s_cur.big), "en %u días", t->faltan);
    }

    snprintf(s_cur.sub1, sizeof(s_cur.sub1), "%s", t->rango);
    if (t->country[0]) snprintf(s_cur.sub2, sizeof(s_cur.sub2), "%s, %s", t->city, t->country);
    else               snprintf(s_cur.sub2, sizeof(s_cur.sub2), "%s", t->city);

    arrancar(MODO_TORNEO);
}

void uiPadelShowScreen(uint8_t screen) {
    if (!s_ready) return;

    const PadelTour* live = padelLive();
    uint8_t i = screen;

    if (live) {
        if (i == 0) { mostrarTorneo(live); return; }
        i--;
    }
    if (i < padelMatchCount()) {
        mostrarPartido(padelMatch(i), live);
        return;
    }
    i -= padelMatchCount();
    if (i < padelTourCount()) {
        mostrarTorneo(padelTour(i));
        return;
    }
    uiPadelShowStatus("Pádel", "No hay torneos cargados todavía.");
}

void uiPadelShowStatus(const char* eyebrow, const char* message) {
    if (!s_ready) return;
    memset(&s_cur, 0, sizeof(s_cur));
    snprintf(s_cur.chip, sizeof(s_cur.chip), "%s", eyebrow ? eyebrow : "PADEL");
    mayusculas(s_cur.chip);

    uiSprite.setFont(DS::fontHeading());
    wrap2(message ? message : "", s_cur.name1, sizeof(s_cur.name1),
          s_cur.name2, sizeof(s_cur.name2), SCREEN_W - 2 * MARGIN);

    arrancar(MODO_ESTADO);
}

bool uiPadelTick(float progress01) {
    if (!s_ready || !s_hasContent) return false;

    const uint32_t now = millis();
    s_progress = progress01 < 0.0f ? 0.0f : (progress01 > 1.0f ? 1.0f : progress01);

    if (s_animating) {
        const uint32_t elapsed = uiAnimElapsed();
        const uint32_t t0 = micros();
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

    if (now - s_lastProgressPaint >= 250) {
        s_lastProgressPaint = now;
        const UiBand b = paintFrame(0xFFFF);
        uiAnimPresent(b.y, b.h);
    }
    return false;
}

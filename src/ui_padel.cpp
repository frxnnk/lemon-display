#include "ui_padel.h"
#include "config.h"
#include "design_system.h"
#include "ui_chrome.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;

// ── Geometría ──
// El eje de la pantalla es el enfrentamiento: las dos parejas son lo único en
// tipografía de título, y todo lo demás —hora, cancha, fase, sede— baja a
// texto de apoyo. Es la misma jerarquía que en noticias, donde el titular manda
// y la fuente y la hora quedan en la cabecera.
static constexpr int MARGIN = FercedChrome::MARGIN;
static constexpr int MARK_W = 11;

static constexpr int CAP_Y   = 96;    // "mié 5 ago · 10:00"
static constexpr int PA_Y    = 150;   // pareja A
static constexpr int PA_Y2   = 190;
static constexpr int RULE_Y  = 246;
static constexpr int PB_Y    = 268;   // pareja B
static constexpr int PB_Y2   = 308;
static constexpr int FOOT_Y  = 378;
static constexpr int RAIL_Y  = FercedChrome::RAIL_Y;

// Modo torneo: el nombre es el héroe y la cuenta regresiva el dato duro, en la
// cursiva con gracias de la marca.
static constexpr int T_NAME_Y = 130;
static constexpr int T_NAME_Y2 = 168;
static constexpr int T_BIG_Y  = 244;
static constexpr int T_SUB_Y  = 330;
static constexpr int T_SUB_Y2 = 360;

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
    char fase[40];               // "R32 · FEMENINO", va en la cejilla
    char dia[24];                // "día 4 de 8", en la cursiva de la marca
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

// Las bandas TESELAN la pantalla: la de un slot empieza antes de que termine la
// del anterior y entre todas cubren de 0 a 480. Eso es lo que hace que una app
// pueda entrar encima de otra, o el modo torneo encima del modo partido, sin
// limpiar nada primero: la ola tapa todo lo viejo a su paso. Antes hacía falta
// un uiAnimInvalidate() al cambiar de modo —99 ms de pantalla negra— porque los
// huecos de un modo caían sobre el contenido del otro.
// Alto real del renglón de cada fuente, para cerrar la banda por abajo. No es
// el tamaño en puntos: un titular de 19 pt ocupa 40 px con las bajas incluidas,
// y cerrar la banda en 28 —el alto de una mayúscula— recortaba los nombres con
// "j" o "g" durante toda la entrada.
static constexpr int ALTO_TITULAR = 40;
static constexpr int ALTO_CUERPO  = 26;
static constexpr int ALTO_ACENTO  = 44;

// La banda de un slot sale de su contenido y de nada más: cuatro píxeles de
// aire arriba, y abajo el alto del renglón más los 22 px que el elemento se
// desplaza mientras entra.
static Row banda(int top, int alto) {
    return Row{(int16_t)(top - 4), (int16_t)(top + alto + UI_RISE_PX)};
}

// Las bandas tienen que TESELAR la pantalla: entre todas, cubrir de 0 a 480 sin
// dejar una fila sin dueño. Eso es lo que hace que una app pueda entrar encima
// de otra, o el modo torneo encima del modo partido, sin limpiar nada primero:
// la ola tapa todo lo viejo a su paso. Antes hacía falta un uiAnimInvalidate()
// al cambiar de modo —99 ms de pantalla negra— porque los huecos de un modo
// caían sobre el contenido del otro.
//
// Cerrar los huecos a mano no funciona. Se intentó y quedaron dos: doce filas
// entre la última línea de apoyo y el riel, y una banda que arrancaba DEBAJO
// del texto que tenía que dibujar, así que el renglón salía cortado por la
// mitad. Los dos aparecieron recién al entrar a la app desde el selector.
//
// Así que no se cierran a mano: cada slot declara dónde está su contenido y
// esta función estira los arranques hasta tocar el cierre del anterior. Un
// hueco deja de ser posible por construcción, y ninguna banda se recorta por
// debajo de lo que tiene que dibujar.
static void cerrarHuecos() {
    s_rows[0].y0 = 0;
    for (uint8_t i = 1; i < SLOTS; i++) {
        if (s_rows[i].y0 > s_rows[i - 1].y1) s_rows[i].y0 = s_rows[i - 1].y1;
    }
    s_rows[SLOTS - 1].y1 = SCREEN_H;
}

static void filas(Modo m) {
    s_rows[0] = banda(FercedChrome::EYEBROW_Y - 14, 62);    // cejilla + regla
    s_rows[6] = banda(RAIL_Y, FercedChrome::RAIL_H);        // riel

    if (m == MODO_PARTIDO) {
        s_rows[1] = banda(CAP_Y,  ALTO_CUERPO);
        s_rows[2] = banda(PA_Y,   PA_Y2 - PA_Y + ALTO_TITULAR);
        s_rows[3] = banda(RULE_Y, 1);
        s_rows[4] = banda(PB_Y,   PB_Y2 - PB_Y + ALTO_TITULAR);
        s_rows[5] = banda(FOOT_Y, ALTO_CUERPO);
    } else {
        s_rows[1] = banda(T_NAME_Y,  ALTO_TITULAR);
        s_rows[2] = banda(T_NAME_Y2, ALTO_TITULAR);
        s_rows[3] = banda(T_BIG_Y,   ALTO_ACENTO);
        s_rows[4] = banda(T_SUB_Y,   ALTO_CUERPO);
        s_rows[5] = banda(T_SUB_Y2,  ALTO_CUERPO);
    }
    cerrarHuecos();
}

// ── Composición ──

static UiBand dirtyBand(uint32_t elapsed) {
    if (elapsed == 0xFFFF) return UiBand{RAIL_Y - 2, FercedChrome::RAIL_H + 4};
    if (elapsed >= uiAnimTotalMs(SLOTS)) return UiBand{RAIL_Y - 2, FercedChrome::RAIL_H + 4};

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

static void pintarCejilla(const UiBand& b, float t) {
    if (!uiAnimTouches(b, s_rows[0].y0, s_rows[0].y1)) return;
    if (t <= 0.0f) return;
    const int dy = (int)((1.0f - t) * UI_RISE_PX);
    uiMark(uiSprite, SCREEN_W - MARGIN - MARK_W, 20 + dy, t);

    uiSprite.setFont(DS::fontCaption());
    if (s_cur.chip[0]) {
        uiSprite.setTextDatum(lgfx::middle_left);
        uiSprite.setTextColor(uiAnimLerp(FG_3, CANVAS, t));
        uiSprite.drawString(s_cur.chip, MARGIN, FercedChrome::EYEBROW_Y + dy);
    }
    if (s_cur.fase[0]) {
        uiSprite.setTextDatum(lgfx::middle_right);
        uiSprite.setTextColor(uiAnimLerp(FG_4, CANVAS, t));
        uiSprite.drawString(s_cur.fase, SCREEN_W - MARGIN - MARK_W - 16,
                            FercedChrome::EYEBROW_Y + dy);
    }
    // La regla aparece pero no sube: es el eje de la composición, no un
    // elemento que llega. Y así no se sale de la banda de su slot.
    uiSprite.drawFastHLine(MARGIN, FercedChrome::RULE_Y, SCREEN_W - 2 * MARGIN,
                           uiAnimLerp(LINE, CANVAS, t));
}

// Una pareja: dos nombres en tipografía de titular, el número de cabeza de serie
// pegado al primero y el resultado a la derecha, en negrita, ocupando el alto de
// las dos líneas. El perdedor de un partido terminado baja entero a FG_3: es la
// forma más barata de que se lea quién ganó sin gastar un color de acento.
static void pintarPareja(const UiBand& b, uint8_t slot, int y1, int y2,
                         const char* n1, const char* n2,
                         const char* seed, const char* score, bool perdedor) {
    if (!uiAnimTouches(b, s_rows[slot].y0, s_rows[slot].y1)) return;
    const float t = uiAnimSlotT(uiAnimElapsed(), slot);
    if (t <= 0.0f) return;
    const int dy = (int)((1.0f - t) * UI_RISE_PX);
    const uint16_t base = perdedor ? FG_3 : FG;

    // El resultado y el número de cabeza de serie se reservan su columna antes
    // de escribir los nombres: con apellidos largos —"Santiago Jose Pineda
    // Cabello"— el nombre llegaba hasta el borde y se montaba encima. El seed
    // cuenta también, porque va pegado al final del primer nombre y si no se
    // descuenta acá se mete justo en la columna del marcador.
    int colScore = 0;
    if (score && score[0]) {
        uiSprite.setFont(DS::fontDataLg());
        colScore = uiSprite.textWidth(score) + 20;
    }
    int colSeed = 0;
    if (seed && seed[0]) {
        uiSprite.setFont(DS::fontCaption());
        colSeed = uiSprite.textWidth(seed) + 10;
    }
    const int libre = SCREEN_W - 2 * MARGIN - colScore;

    uiSprite.setFont(DS::fontTitular());
    uiSprite.setTextDatum(lgfx::top_left);
    uiSprite.setTextColor(uiAnimLerp(base, CANVAS, t));
    // El seed sólo le come ancho al primer nombre, que es al lado de quien va.
    // Descontárselo también al segundo lo recortaba sin motivo.
    const int w1 = uiTextoRecortado(uiSprite, n1, MARGIN, y1 + dy, libre - colSeed);
    if (n2 && n2[0]) uiTextoRecortado(uiSprite, n2, MARGIN, y2 + dy, libre);

    if (seed && seed[0]) {
        uiSprite.setFont(DS::fontCaption());
        uiSprite.setTextDatum(lgfx::top_left);
        uiSprite.setTextColor(uiAnimLerp(FG_4, CANVAS, t));
        uiSprite.drawString(seed, MARGIN + w1 + 10, y1 + dy + 8);
    }
    if (score && score[0]) {
        uiSprite.setFont(DS::fontDataLg());
        uiSprite.setTextDatum(lgfx::middle_right);
        uiSprite.setTextColor(uiAnimLerp(base, CANVAS, t));
        uiSprite.drawString(score, SCREEN_W - MARGIN, (y1 + y2) / 2 + 14 + dy);
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

    pintarCejilla(b, uiAnimSlotT(elapsed, 0));

    if (s_modo == MODO_PARTIDO) {
        // El cuándo: fecha y hora. "EN JUEGO" en verde es uno de los pocos usos
        // legítimos del acento, porque sí es un estado.
        pintarLinea(b, 1, CAP_Y, s_cur.cap1, DS::fontBody(),
                    s_cur.jugando ? SUCCESS : FG_2);

        pintarPareja(b, 2, PA_Y, PA_Y2, s_cur.a1, s_cur.a2,
                     s_cur.seedA, s_cur.scoreA, s_cur.ganador == 2);

        if (uiAnimTouches(b, s_rows[3].y0, s_rows[3].y1)) {
            const float t = uiAnimSlotT(elapsed, 3);
            // La regla corta entre las dos parejas tampoco sube: es un separador.
            if (t > 0.0f) {
                uiSprite.drawFastHLine(MARGIN, RULE_Y, 64, uiAnimLerp(LINE, CANVAS, t));
            }
        }

        pintarPareja(b, 4, PB_Y, PB_Y2, s_cur.b1, s_cur.b2,
                     s_cur.seedB, s_cur.scoreB, s_cur.ganador == 1);

        // El pie: dónde a la izquierda, qué día del torneo a la derecha en la
        // cursiva con gracias. Son dos strings separados y no uno solo porque
        // mezclar dos fuentes en un drawString no existe.
        pintarLinea(b, 5, FOOT_Y, s_cur.pie, DS::fontBody(), FG_3);
        if (s_cur.dia[0] && uiAnimTouches(b, s_rows[5].y0, s_rows[5].y1)) {
            const float t = uiAnimSlotT(elapsed, 5);
            if (t > 0.0f) {
                const int dy = (int)((1.0f - t) * UI_RISE_PX);
                uiSprite.setFont(DS::fontAcentoSm());
                uiSprite.setTextDatum(lgfx::top_right);
                uiSprite.setTextColor(uiAnimLerp(FG_3, CANVAS, t));
                uiSprite.drawString(s_cur.dia, SCREEN_W - MARGIN, FOOT_Y + dy + 2);
            }
        }
    } else {
        pintarLinea(b, 1, T_NAME_Y, s_cur.name1, DS::fontTitular(), FG);
        pintarLinea(b, 2, T_NAME_Y2, s_cur.name2, DS::fontTitular(), FG);
        // La cuenta regresiva es el dato duro de la pantalla y va en la cursiva
        // con gracias, que es el gesto que ferced.com repite en cada sección.
        pintarLinea(b, 3, T_BIG_Y, s_cur.big, DS::fontAcento(),
                    s_modo == MODO_ESTADO ? FG_2 : FG);
        pintarLinea(b, 4, T_SUB_Y, s_cur.sub1, DS::fontBody(), FG_2);
        pintarLinea(b, 5, T_SUB_Y2, s_cur.sub2, DS::fontBody(), FG_3);
    }

    const float railT = uiAnimSlotT(elapsed, 6);
    if (railT > 0.0f) uiRail(uiSprite, RAIL_Y, s_progress, railT);
    return b;
}

// ── API ──

static void arrancar(Modo m) {
    // Los dos modos tienen filas en lugares distintos, pero las bandas de ambos
    // teselan la pantalla entera, así que la ola tapa lo que había sin que haga
    // falta un borrado previo. Antes acá iba un uiAnimInvalidate() al cambiar de
    // modo, que costaba 99 ms de panel en negro.
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

    // El primer renglón es el CUÁNDO: la hora sola no alcanza, porque el orden
    // de juego es siempre de una fecha concreta y en pantalla no hay forma de
    // saber cuál. La cancha bajó al pie, que es donde va el dónde.
    s_cur.jugando = m->state == 1;
    const char* cuando = s_cur.jugando ? "EN JUEGO"
                                       : (m->time[0] ? m->time : "sin horario");
    const char* fecha = padelFecha();
    if (fecha[0]) snprintf(s_cur.cap1, sizeof(s_cur.cap1), "%s  ·  %s", fecha, cuando);
    else          snprintf(s_cur.cap1, sizeof(s_cur.cap1), "%s", cuando);

    const char* gen = m->gender == 'F' ? "FEMENINO" : (m->gender == 'M' ? "MASCULINO" : "");
    if (m->round[0] && gen[0]) snprintf(s_cur.fase, sizeof(s_cur.fase), "%s  ·  %s", m->round, gen);
    else if (m->round[0])      snprintf(s_cur.fase, sizeof(s_cur.fase), "%s", m->round);
    else                       snprintf(s_cur.fase, sizeof(s_cur.fase), "%s", gen);

    snprintf(s_cur.a1, sizeof(s_cur.a1), "%s", m->a1);
    snprintf(s_cur.a2, sizeof(s_cur.a2), "%s", m->a2);
    snprintf(s_cur.b1, sizeof(s_cur.b1), "%s", m->b1);
    snprintf(s_cur.b2, sizeof(s_cur.b2), "%s", m->b2);
    if (m->seedA[0]) snprintf(s_cur.seedA, sizeof(s_cur.seedA), "(%s)", m->seedA);
    if (m->seedB[0]) snprintf(s_cur.seedB, sizeof(s_cur.seedB), "(%s)", m->seedB);
    snprintf(s_cur.scoreA, sizeof(s_cur.scoreA), "%s", m->scoreA);
    snprintf(s_cur.scoreB, sizeof(s_cur.scoreB), "%s", m->scoreB);
    if (m->state == 2) s_cur.ganador = ganador(m->scoreA, m->scoreB);

    // Pie: dónde a la izquierda. La ciudad ya la dice la cejilla del torneo
    // ("LONDON P1"), así que acá va la cancha, que es lo único que no está en
    // ningún otro lado. El día del torneo se va a la derecha, en cursiva.
    if (m->court[0]) snprintf(s_cur.pie, sizeof(s_cur.pie), "%s", m->court);
    if (live && live->days > 0) {
        snprintf(s_cur.dia, sizeof(s_cur.dia), "día %u de %u", live->day, live->days);
    }

    arrancar(MODO_PARTIDO);
}

static void mostrarTorneo(const PadelTour* t) {
    memset(&s_cur, 0, sizeof(s_cur));

    chipTorneo(s_cur.chip, sizeof(s_cur.chip),
               t->live ? "EN JUEGO" : "PRÓXIMO", t->cat, t->name);
    mayusculas(s_cur.chip);

    uiSprite.setFont(DS::fontTitular());
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

    uiSprite.setFont(DS::fontTitular());
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

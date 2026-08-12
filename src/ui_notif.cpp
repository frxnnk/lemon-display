#include "ui_notif.h"
#include "config.h"
#include "design_system.h"
#include "ui_chrome.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;
using namespace FercedChrome;

// Se compone sobre el sprite y se muestra con uiAnimReveal(), igual que el
// resto. Ver la nota de ui_todo.cpp sobre por qué ninguna pantalla dibuja ya
// directo sobre tft.

namespace {

LGFX_Sprite& g = uiSprite;

constexpr int MARK_W = 11;

// El papel del globo. Es FG, el blanco de la paleta: el aparato ya usa el
// negativo —relleno blanco, texto en el canvas— para marcar la app activa y la
// tarea hecha, asi que un globo blanco no inventa un tono, usa el que ya hay.
// Los grises sobre papel se derivan mezclando contra el, igual que los grises
// sobre canvas se derivan mezclando contra el canvas.
constexpr uint16_t PAPEL = FG;

// ── El aviso que llega: el agente asoma y te habla ──
//
// No es una pantalla, es alguien que aparece en un rincón. El personaje —la
// marca del agente a 56 px— se para abajo a la derecha y el globo de diálogo
// sale de él hacia arriba y a la izquierda, con la cola apuntándolo. Arriba
// sigue viéndose el titular, el partido o la lista que estabas mirando.
//
// Puede dibujarse encima porque desde la 1.4.0 todas las pantallas componen
// sobre el sprite: el sprite ya tiene lo que está en el panel, así que alcanza
// con pintar el globo arriba y no hay que borrar ni redibujar nada.
//
// Antes esto ocupaba la pantalla entera durante 25 segundos, que para un
// "terminé la tarea" es tapar todo para decir cuatro palabras.
constexpr int PERS_D  = 56;                          // el personaje
constexpr int PERS_X  = SCREEN_W - 26 - PERS_D;      // 398
constexpr int PERS_Y  = SCREEN_H - 24 - PERS_D;      // 400

constexpr int GLOBO_X = 24;
constexpr int GLOBO_Y = 208;
constexpr int GLOBO_W = SCREEN_W - 2 * GLOBO_X;      // 432
constexpr int GLOBO_H = PERS_Y - GLOBO_Y - 24;       // 168
constexpr int GLOBO_R = 26;
constexpr int PAD     = 24;
constexpr int IN_X    = GLOBO_X + PAD;               // 48
constexpr int IN_W    = GLOBO_W - 2 * PAD;           // 384

constexpr int FUENTE_Y = GLOBO_Y + 26;               // 234
constexpr int TIT_Y    = GLOBO_Y + 48;               // 256
constexpr int TIT_LH   = 38;
constexpr int CUE_LH   = 26;
constexpr int RIEL_Y   = GLOBO_Y + GLOBO_H - 24;     // 352

// La ✕ para cerrar, arriba a la derecha del globo. Redonda y chica: no le come
// renglones al mensaje, que es lo que importa. El aspa va dibujada a mano, como
// el tilde de las tareas: las fuentes no traen el glifo.
constexpr int CERRAR_D = 34;
constexpr int CERRAR_X = GLOBO_X + GLOBO_W - PAD - CERRAR_D;
constexpr int CERRAR_Y = GLOBO_Y + 20;

// ── Burbuja de cada fila de la lista ──
constexpr int FILA_BURB_W = 38;
constexpr int FILA_BURB_H = 32;
constexpr int FILA_TEXT_X = MARGIN + FILA_BURB_W + 16;

// ── Lista ──
constexpr int FILA_Y0 = 100;
// Cinco de 62 y no seis de 54: con 54 el título de un aviso —26 px en
// fontHeading— terminaba a 4 px de la fuente del siguiente y la lista se leía
// como un bloque. La sexta se cuenta igual en el "+N más".
constexpr int FILA_H  = 62;
constexpr int MAX_FILAS = 5;   // 100 + 5*62 = 410, y el riel va en 452

uint8_t s_filas = 0;
bool    s_ready = false;

// Corta por ancho real en píxeles y devuelve cuántos renglones usó. Los avisos
// vienen de la salida de un agente: pueden ser una palabra o una parrafada.
int envolver(const char* texto, int x, int y, int lh, int maxW, int maxLineas,
             const lgfx::IFont* font) {
    if (!texto || !texto[0]) return 0;

    g.setFont(font);
    g.setTextDatum(lgfx::top_left);

    char linea[160] = {0};
    int usados = 0;
    const char* p = texto;

    while (*p && usados < maxLineas) {
        const char* fin = p;
        while (*fin && *fin != ' ') fin++;
        const int largo = (int)(fin - p);
        if (largo <= 0) { p = *fin ? fin + 1 : fin; continue; }

        char cand[160];
        if (linea[0]) snprintf(cand, sizeof(cand), "%s %.*s", linea, largo, p);
        else          snprintf(cand, sizeof(cand), "%.*s", largo, p);

        if (g.textWidth(cand) <= maxW) {
            snprintf(linea, sizeof(linea), "%s", cand);
        } else if (linea[0]) {
            g.drawString(linea, x, y + usados * lh);
            usados++;
            snprintf(linea, sizeof(linea), "%.*s", largo, p);
        } else {
            // Una sola palabra más ancha que la pantalla: se dibuja igual y se
            // sale por el margen antes que tragarse el aviso entero.
            g.drawString(cand, x, y + usados * lh);
            usados++;
            linea[0] = '\0';
        }
        p = *fin ? fin + 1 : fin;
    }
    if (linea[0] && usados < maxLineas) {
        g.drawString(linea, x, y + usados * lh);
        usados++;
    }
    return usados;
}

void hace(char* out, size_t n, uint32_t ms) {
    const uint32_t s = (millis() - ms) / 1000;
    if (s < 10)    { snprintf(out, n, "recién"); return; }
    if (s < 60)    { snprintf(out, n, "hace %lu s", (unsigned long)s); return; }
    if (s < 3600)  { snprintf(out, n, "hace %lu min", (unsigned long)(s / 60)); return; }
    snprintf(out, n, "hace %lu h", (unsigned long)(s / 3600));
}

void mayusculas(char* s) {
    for (; *s; s++) *s = toupper((unsigned char)*s);
}

// La burbuja de chat con la marca del agente adentro. Es lo que hace que se
// entienda de un vistazo QUIÉN habló, sin leer: el destello coral es Claude y
// el nudo blanco es Codex. Una fuente sin marca cae en su inicial, que es lo
// que va a pasar con cualquier cosa que alguien conecte después.
//
// Leído y sin leer se distinguen por la burbuja —maciza contra contorno— y por
// la intensidad de la marca. Contraste, no color, como en el resto del aparato:
// el color que hay acá es el de la marca ajena, no un acento de estado.
void burbujaAgente(int x, int y, int w, int h, const char* src, bool leido) {
    const int lado = h >= 44 ? 34 : 20;
    uiBurbuja(g, x, y, w, h, leido ? LINE : SURFACE, leido ? CANVAS : SURFACE);

    const uint16_t fondo = leido ? CANVAS : SURFACE;
    const float intensidad = leido ? 0.45f : 1.0f;
    const MarcaAgente* m = marcaDeAgente(src);

    if (m) {
        uiMarcaAgente(g, lado == 34 ? m->alfa34 : m->alfa20, lado,
                      x + (w - lado) / 2, y + (h - lado) / 2,
                      m->color, fondo, intensidad);
        return;
    }
    const char inicial[2] = {src && src[0] ? (char)toupper((unsigned char)src[0]) : '?', '\0'};
    g.setFont(lado == 34 ? DS::fontTitular() : DS::fontHeading());
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(uiAnimLerp(FG_2, fondo, intensidad), fondo);
    g.drawString(inicial, x + w / 2, y + h / 2);
}

}  // namespace

void uiNotifSetup() { uiAnimSetup(); s_ready = true; }

// El riel del globo. Va sobre papel blanco, no sobre el canvas, así que su
// carril no puede ser el LINE de siempre —blanco 10% sobre blanco es nada— y se
// deriva igual que el resto de los grises, mezclando contra el papel.
static void rielGlobo(float restante01) {
    if (restante01 < 0.0f) restante01 = 0.0f;
    if (restante01 > 1.0f) restante01 = 1.0f;
    const int lleno = (int)(IN_W * restante01 + 0.5f);
    g.fillRect(IN_X, RIEL_Y, IN_W, RAIL_H, uiAnimLerp(CANVAS, PAPEL, 0.12f));
    for (int i = 0; i < lleno; i++) {
        const float t = lleno > 1 ? (float)i / (float)(lleno - 1) : 1.0f;
        g.drawFastVLine(IN_X + i, RIEL_Y, RAIL_H, fercedSpectrum(t));
    }
}

// Se agota hasta el cierre automático. Repinta sólo su renglón —dos píxeles de
// alto— cuatro veces por segundo: repintar el globo entero sería medio segundo
// de dibujo para los nueve que dura el aviso.
void uiNotifDrawBarra(float restante01) {
    if (!s_ready) return;
    rielGlobo(restante01);
    uiAnimPresent(RIEL_Y - 2, RAIL_H + 4);
}

void uiNotifDrawCard(const Notif* n, float restante01) {
    if (!s_ready || !n) return;

    // Sin fillScreen: esto va ENCIMA de lo que había. Ver la nota de la
    // geometría.

    // La cola primero, para que el globo le pise el borde de arriba y los dos
    // queden como una sola pieza.
    const int cx = PERS_X + PERS_D / 2;
    g.fillTriangle(cx - 26, GLOBO_Y + GLOBO_H - 12, cx + 8, GLOBO_Y + GLOBO_H - 12,
                   cx - 4, PERS_Y - 4, PAPEL);
    g.fillSmoothRoundRect(GLOBO_X, GLOBO_Y, GLOBO_W, GLOBO_H, GLOBO_R, PAPEL);

    // El personaje. Es la marca del agente, en su color, parada sobre el canvas:
    // el que habla no vive adentro del globo.
    const MarcaAgente* m = marcaDeAgente(n->src);
    if (m) {
        uiMarcaAgente(g, m->alfa56, PERS_D, PERS_X, PERS_Y, m->color, CANVAS, 1.0f);
    } else {
        const char inicial[2] = {n->src[0] ? (char)toupper((unsigned char)n->src[0]) : '?', '\0'};
        g.fillSmoothRoundRect(PERS_X, PERS_Y, PERS_D, PERS_D, 18, SURFACE);
        g.setFont(DS::fontTitular());
        g.setTextDatum(lgfx::middle_center);
        g.setTextColor(FG_2, SURFACE);
        g.drawString(inicial, PERS_X + PERS_D / 2, PERS_Y + PERS_D / 2);
    }

    // Quién habla, chiquito arriba del mensaje.
    char fuente[NOTIF_SRC_LEN + 4];
    snprintf(fuente, sizeof(fuente), "%s", n->src);
    mayusculas(fuente);
    g.setFont(DS::fontCaption());
    g.setTextDatum(lgfx::top_left);
    g.setTextColor(uiAnimLerp(CANVAS, PAPEL, 0.45f), PAPEL);
    g.drawString(fuente, IN_X, FUENTE_Y);

    // El mensaje, en negro sobre el papel. Dos renglones como mucho: si no entra
    // en dos, el aviso estaba mal escrito y para eso está la lista.
    g.setTextColor(CANVAS, PAPEL);
    const int usados = envolver(n->title, IN_X, TIT_Y, TIT_LH,
                                IN_W - CERRAR_D - 12, 2, DS::fontTitular());

    // El detalle, si el mensaje entró en un renglón.
    if (n->body[0] && usados <= 1) {
        g.setTextColor(uiAnimLerp(CANVAS, PAPEL, 0.40f), PAPEL);
        envolver(n->body, IN_X, TIT_Y + TIT_LH + 8, CUE_LH, IN_W, 1, DS::fontBody());
    }

    // La ✕, dibujada a mano.
    const int ccx = CERRAR_X + CERRAR_D / 2, ccy = CERRAR_Y + CERRAR_D / 2;
    g.fillSmoothCircle(ccx, ccy, CERRAR_D / 2, uiAnimLerp(CANVAS, PAPEL, 0.08f));
    const uint16_t aspa = uiAnimLerp(CANVAS, PAPEL, 0.55f);
    for (int t = 0; t < 2; t++) {
        g.drawLine(ccx - 6, ccy - 6 + t, ccx + 6, ccy + 6 + t, aspa);
        g.drawLine(ccx + 6, ccy - 6 + t, ccx - 6, ccy + 6 + t, aspa);
    }

    rielGlobo(restante01);
    uiAnimReveal();
}

void uiNotifDrawLista() {
    if (!s_ready) return;

    g.fillScreen(CANVAS);

    const uint8_t total = notifCount();
    const uint8_t sinLeer = notifSinLeer();
    char cuenta[32] = {0};
    if (total == 0)       snprintf(cuenta, sizeof(cuenta), "VACÍA");
    else if (sinLeer == 0) snprintf(cuenta, sizeof(cuenta), "AL DÍA");
    else                   snprintf(cuenta, sizeof(cuenta), "%u SIN LEER", sinLeer);

    uiEyebrow(g, "AVISOS", cuenta, 1.0f);
    uiMark(g, SCREEN_W - MARGIN - MARK_W, 20, 1.0f);

    s_filas = total < MAX_FILAS ? total : MAX_FILAS;

    if (total == 0) {
        g.setTextDatum(lgfx::top_left);
        g.setFont(DS::fontAcento());
        g.setTextColor(FG_2, CANVAS);
        g.drawString("nada nuevo.", MARGIN, 190);
        g.setFont(DS::fontBody());
        g.setTextColor(FG_3, CANVAS);
        g.drawString("Los agentes avisan acá cuando", MARGIN, 250);
        g.drawString("terminan una tarea.", MARGIN, 278);
        uiRail(g, RAIL_Y, 0.0f);
        uiAnimReveal();
        return;
    }

    for (uint8_t i = 0; i < s_filas; i++) {
        const Notif* n = notifAt(i);
        if (!n) continue;
        const int y = FILA_Y0 + i * FILA_H;

        // Burbuja maciza = sin leer, de contorno = leído. Reemplaza al punto
        // que había: dice lo mismo y además dice quién.
        burbujaAgente(MARGIN, y + 2, FILA_BURB_W, FILA_BURB_H, n->src, n->leido);

        g.setTextDatum(lgfx::top_left);
        g.setFont(DS::fontCaption());
        g.setTextColor(n->leido ? FG_4 : FG_3, CANVAS);
        g.drawString(n->src, FILA_TEXT_X, y + 4);

        char cuando[24];
        hace(cuando, sizeof(cuando), n->ms);
        g.setFont(DS::fontAcentoSm());
        g.setTextDatum(lgfx::top_right);
        g.drawString(cuando, SCREEN_W - MARGIN, y + 2);

        // Un solo renglón por aviso: la lista es para repasar, no para leer.
        g.setTextDatum(lgfx::top_left);
        g.setFont(DS::fontHeading());
        g.setTextColor(n->leido ? FG_3 : FG, CANVAS);
        uiTextoRecortado(g, n->title, FILA_TEXT_X, y + 26,
                         SCREEN_W - MARGIN - FILA_TEXT_X);
    }

    if (total > MAX_FILAS) {
        g.setFont(DS::fontCaption());
        g.setTextDatum(lgfx::top_left);
        g.setTextColor(FG_4, CANVAS);
        char mas[32];
        snprintf(mas, sizeof(mas), "+%u más", (unsigned)(total - MAX_FILAS));
        g.drawString(mas, FILA_TEXT_X, FILA_Y0 + MAX_FILAS * FILA_H);
    }

    // Sin leer sobre el total: el riel dice de un vistazo cuánto queda por mirar.
    uiRail(g, RAIL_Y, total > 0 ? (float)(total - sinLeer) / (float)total : 0.0f);
    uiAnimReveal();
}

bool uiNotifHitCerrar(int16_t x, int16_t y) {
    // Se acepta un poco mas de area que la pastilla dibujada: el minimo tactil
    // del design system son 44 px y el dedo no cae donde uno cree.
    return x >= CERRAR_X - 10 && x < CERRAR_X + CERRAR_D + 10 &&
           y >= CERRAR_Y - 10 && y < CERRAR_Y + CERRAR_D + 10;
}

int8_t uiNotifHit(int16_t x, int16_t y) {
    if (x < 0 || x >= SCREEN_W) return -1;
    if (y < FILA_Y0) return -1;
    const int i = (y - FILA_Y0) / FILA_H;
    if (i < 0 || i >= s_filas) return -1;
    return (int8_t)i;
}

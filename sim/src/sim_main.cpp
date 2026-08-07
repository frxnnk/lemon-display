// Entrada del simulador. Maneja la misma maquina de rotacion que el firmware
// para que el ritmo sea el mismo, y agrega atajos de teclado para iterar.

#include "sim_display.h"
#include "apps.h"
#include "feed_client.h"
#include "padel_client.h"
#include "notif_store.h"
#include "todo_store.h"
#include "ui_config.h"
#include "ui_ferced.h"
#include "ui_launcher.h"
#include "ui_provision.h"
#include <qrcode.h>
#include "ui_notif.h"
#include "ui_padel.h"
#include "ui_todo.h"

#include <SDL2/SDL.h>

// Vive en sim_notif.cpp: carga el fixture de avisos.
void simNotifLoad();

#include <chrono>
#include <cstdio>
#include <cstdlib>

static uint8_t  s_index = 0;
static uint8_t  s_padelIndex = 0;
static uint8_t  s_app = APP_NOTICIAS;
static uint32_t s_lastRotate = 0;
static bool     s_paused = false;
static uint32_t s_rotateMs = 17000;
// Mientras una pantalla estatica esta arriba hay que frenar el tick del feed:
// si no, la animacion repinta encima y la captura sale con el titular.
static bool     s_estatica = false;

// Configuracion por linea de comandos. El teclado no sirve para automatizar:
// Panel_sdl corre su propio bucle de eventos en el hilo principal y consume
// las teclas antes de que las vea este loop.
int      g_startIndex = 0;
bool     g_still      = false;   // sin rotacion, para capturas estables
bool     g_config     = false;   // arrancar en la pantalla de configuracion
int      g_progreso   = -1;      // 0..100: congela la franja de estado del OTA
bool     g_padel      = false;   // arrancar en la app de padel
bool     g_launcher   = false;   // arrancar en el selector de apps

// Dibuja PRIMERO el selector y recién después entra a la app pedida, que es lo
// que pasa de verdad cuando el usuario elige una app. Sirve para lo único que no
// se puede verificar mirando una pantalla sola: que las bandas del escalonado
// cubran las 480 filas. Si alguna fila no tiene dueño, en la captura queda un
// jirón del selector —que es una pantalla llena y clara— sobre el fondo de la
// app nueva, y se ve de lejos.
bool     g_desdeLauncher = false;
bool     g_setup      = false;   // la pantalla del QR de aprovisionamiento
bool     g_tareas     = false;   // arrancar en la lista de tareas
bool     g_avisos     = false;   // arrancar en la lista de avisos
bool     g_aviso      = false;   // la tarjeta que interrumpe

// Captura de una transición a mitad de camino: muestra el ítem --item, deja que
// termine de entrar, pasa al --hacia y congela el dibujo a los --congelar ms.
// Sin esto la animación sólo se puede mirar de reojo, y lo que hay que
// verificar —que la pantalla NUNCA quede vacía— pasa justo en el medio.
int      g_hacia      = -1;
int      g_congelar   = -1;

// El firmware usa millis(); aca lo replico sobre el reloj del sistema.
static uint32_t millisNow() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

static uint32_t nowEpoch() { return (uint32_t)time(nullptr); }

// Datos falsos: lo que se verifica aca es la disposicion, no los valores.
static void mostrarConfig() {
    static const ConfigInfo demo = {
        "Fran",
        "1.1.0", "e96efeb-dirty", "2026-08-05 04:20",
        "MiWiFi", "192.168.1.41", "feed.ferced.com",
        -62, 8073, 35.4f, 20, true
    };
    s_estatica = true;
    uiConfigDraw(demo);

    // El mismo llamado que hace el callback de progreso del OTA en el firmware,
    // con la version falsa: aca no se descarga nada, se mira como queda.
    if (g_progreso >= 0) uiConfigEstado("Descargando 1.1.1", g_progreso);
}

// La pantalla de aprovisionamiento con el QR de verdad. El simulador compila la
// misma libreria de QR que el firmware, asi que lo que se ve aca es lo que se
// va a ver en el aparato —y se puede escanear del monitor para comprobar que el
// codigo es valido, que es la unica parte que el layout no dice—.
static void mostrarSetup() {
    static uint8_t modulos[41 * 41];
    QRCode qr;
    // La version 6 son 41x41 modulos; el buffer de la libreria no es constexpr,
    // asi que se dimensiona con el maximo y se le pasa ese.
    static uint8_t datos[1024];
    qrcode_initText(&qr, datos, 6, ECC_LOW, "WIFI:S:Ferced-Fran;T:WPA;P:ferced1234;;");
    const uint8_t lado = qr.size < 41 ? qr.size : 41;
    for (uint8_t y = 0; y < lado; y++) {
        for (uint8_t x = 0; x < lado; x++) {
            modulos[y * lado + x] = qrcode_getModule(&qr, x, y) ? 1 : 0;
        }
    }
    s_estatica = true;
    uiProvisionDraw(modulos, lado, "Fran", "Ferced-Fran", "ferced1234",
                    "http://192.168.4.1");
}

static void mostrarLauncher() {
    uiAnimCurtainOnce();   // igual que enterLauncher() en el firmware
    AppInfo apps[APP_COUNT];
    apps[APP_NOTICIAS].nombre = "NOTICIAS";
    apps[APP_NOTICIAS].inicial = 'N';
    std::snprintf(apps[APP_NOTICIAS].estado, sizeof(apps[APP_NOTICIAS].estado),
                  "%u titulares", feedCount());

    apps[APP_PADEL].nombre = "PÁDEL";
    apps[APP_PADEL].inicial = 'P';
    const PadelTour* live = padelLive();
    if (live) {
        std::snprintf(apps[APP_PADEL].estado, sizeof(apps[APP_PADEL].estado),
                      "%s  ·  día %u de %u", live->name, live->day, live->days);
    } else if (padelTourCount() > 0) {
        std::snprintf(apps[APP_PADEL].estado, sizeof(apps[APP_PADEL].estado),
                      "próximo: %s", padelTour(0)->name);
    } else {
        std::snprintf(apps[APP_PADEL].estado, sizeof(apps[APP_PADEL].estado), "sin datos");
    }

    apps[APP_TAREAS].nombre = "TAREAS";
    apps[APP_TAREAS].inicial = 'T';
    const uint8_t pend = todoPending();
    if (todoCount() == 0) {
        std::snprintf(apps[APP_TAREAS].estado, sizeof(apps[APP_TAREAS].estado), "sin tareas");
    } else if (pend == 0) {
        std::snprintf(apps[APP_TAREAS].estado, sizeof(apps[APP_TAREAS].estado), "todo hecho");
    } else {
        std::snprintf(apps[APP_TAREAS].estado, sizeof(apps[APP_TAREAS].estado),
                      "%u de %u pendiente%s", pend, todoCount(), pend == 1 ? "" : "s");
    }

    apps[APP_AVISOS].nombre = "AVISOS";
    apps[APP_AVISOS].inicial = 'A';
    const uint8_t sinLeer = notifSinLeer();
    if (notifCount() == 0) {
        std::snprintf(apps[APP_AVISOS].estado, sizeof(apps[APP_AVISOS].estado), "sin avisos");
    } else if (sinLeer == 0) {
        std::snprintf(apps[APP_AVISOS].estado, sizeof(apps[APP_AVISOS].estado),
                      "%u, todos leídos", notifCount());
    } else {
        std::snprintf(apps[APP_AVISOS].estado, sizeof(apps[APP_AVISOS].estado),
                      "%u sin leer", sinLeer);
    }

    s_estatica = true;
    uiLauncherDraw(apps, APP_COUNT, s_app);
}

static void show() {
    s_estatica = false;
    if (s_app == APP_TAREAS) {
        // Igual que en el firmware: la lista es una pantalla quieta, así que
        // se para el tick para que la animación no la repinte encima.
        s_estatica = true;
        uiTodoDraw("192.168.1.41");
        return;
    }
    if (s_app == APP_AVISOS) {
        s_estatica = true;
        uiNotifDrawLista();
        return;
    }
    if (s_app == APP_PADEL) {
        const uint8_t total = padelScreenCount();
        if (total == 0) {
            uiPadelShowStatus("Padel", "El fixture esta vacio. Corre padelcheck -fixture.");
            return;
        }
        if (s_padelIndex >= total) s_padelIndex = 0;
        uiPadelShowScreen(s_padelIndex);
        return;
    }

    const uint8_t total = feedCount();
    if (total == 0) {
        uiFercedShowStatus("Sin contenido", "El fixture esta vacio. Corre tools/fetch_fixture.py.");
        return;
    }
    if (s_index >= total) s_index = 0;
    uiFercedShowItem(feedItem(s_index), s_index, total, false);
}

static void advance(int delta) {
    if (s_app == APP_TAREAS || s_app == APP_AVISOS) return;   // una lista no rota
    if (s_app == APP_PADEL) {
        const uint8_t total = padelScreenCount();
        if (total == 0) return;
        s_padelIndex = (uint8_t)((s_padelIndex + total + delta) % total);
    } else {
        const uint8_t total = feedCount();
        if (total == 0) return;
        s_index = (uint8_t)((s_index + total + delta) % total);
    }
    show();
    s_lastRotate = millisNow();
}

static const char* nombreApp(uint8_t a) {
    switch (a) {
        case APP_PADEL:  return "padel";
        case APP_TAREAS: return "tareas";
        case APP_AVISOS: return "avisos";
        default:         return "noticias";
    }
}

static void cambiarApp() {
    s_app = (uint8_t)((s_app + 1) % APP_COUNT);
    std::printf("  app: %s\n", nombreApp(s_app));
    // Lo mismo que hace enterApp() en el firmware. Si el simulador no pidiera la
    // cortina, las transiciones entre apps se verian aca distinto de como se ven
    // en el aparato, que es justo lo que un simulador no puede permitirse.
    uiAnimCurtainOnce();
    show();
    s_lastRotate = millisNow();
}

static void applyArgs() {
    if (g_padel)  s_app = APP_PADEL;
    if (g_tareas) s_app = APP_TAREAS;
    if (g_avisos) s_app = APP_AVISOS;
    if (g_startIndex > 0) {
        if (s_app == APP_PADEL) {
            const uint8_t total = padelScreenCount();
            if (total > 0) s_padelIndex = (uint8_t)(g_startIndex % total);
        } else {
            const uint8_t total = feedCount();
            if (total > 0) s_index = (uint8_t)(g_startIndex % total);
        }
    }
    if (g_still) s_paused = true;
}

static void banner() {
    std::printf(
        "\n  ferced-display  simulador\n"
        "  ---------------------------------------------\n"
        "  ESPACIO  siguiente item        P  pausar rotacion\n"
        "  B        item anterior         R  repetir animacion\n"
        "  A        cambiar de app        L  selector de apps\n"
        "  C        pantalla de configuracion\n"
        "  T        modelo de tiempo del hardware on/off\n"
        "  F        reporte de framerate\n"
        "  ESC      salir\n\n"
        "  El modelo de tiempo esta PRENDIDO: reproduce el costo\n"
        "  medido en la caja (0,1575 ms por fila + VSync de 23,64 ms).\n"
        "  Apagarlo muestra la animacion ideal, que NO es lo que se ve\n"
        "  en el aparato.\n\n");
}

static void report() {
    const SimStats st = simStats();
    if (st.frames == 0) { std::printf("  [fps] sin frames todavia\n"); return; }
    const double avg = st.sumMs / st.frames;
    std::printf("  [fps] %u frames, medio %.1f ms (%.0f fps), peor %.1f ms  [modelo %s]\n",
                st.frames, avg, 1000.0 / avg, st.worstMs,
                simTimingEnabled() ? "ON" : "OFF");
    simResetStats();
}

void setup() {
    displaySetup();
    displaySetupVSync();
    uiFercedSetup();
    uiPadelSetup();
    uiLauncherSetup();
    uiTodoSetup();
    uiNotifSetup();
    banner();

    feedFetch();
    padelFetch();
    todoLoad();
    simNotifLoad();
    applyArgs();
    if (g_desdeLauncher) mostrarLauncher();
    show();
    s_lastRotate = millisNow();

    // Se deja terminar la entrada del primer ítem, se pasa al segundo y se
    // congela: así la captura sale exactamente en el medio de la transición.
    if (g_hacia >= 0) {
        const uint32_t fin = millisNow() + 2000;
        while (millisNow() < fin) uiFercedTick(nowEpoch(), 0.0f);

        const uint8_t total = feedCount();
        if (total > 0) {
            s_index = (uint8_t)(g_hacia % total);
            uiFercedShowItem(feedItem(s_index), s_index, total, false);
        }
        const uint32_t corte = millisNow() + (g_congelar > 0 ? (uint32_t)g_congelar : 250);
        while (millisNow() < corte) uiFercedTick(nowEpoch(), 0.0f);
        s_estatica = true;
        std::printf("  transicion congelada a los %d ms\n", g_congelar > 0 ? g_congelar : 250);
    }

    if (g_aviso) {
        // Se deja terminar de entrar la pantalla de abajo ANTES de mostrar el
        // aviso. Sin esto el sprite estaba en negro y el globo salia flotando
        // sobre nada, que es justo lo contrario de lo que hay que verificar: el
        // aviso se dibuja ENCIMA de lo que estabas mirando y esa es toda la
        // gracia. El simulador tiene que mostrar eso o no sirve.
        const uint32_t fin = millisNow() + 1600;
        while (millisNow() < fin) uiFercedTick(nowEpoch(), 0.45f);
        s_estatica = true;
        uiNotifDrawCard(notifPendiente(), 0.62f);
    }
    if (g_setup) mostrarSetup();
    if (g_config) mostrarConfig();
    if (g_launcher) mostrarLauncher();
}

void loop() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type != SDL_KEYDOWN) continue;
        switch (e.key.keysym.sym) {
            case SDLK_SPACE:  advance(+1); break;
            case SDLK_b:      advance(-1); break;
            case SDLK_r:      s_estatica = false; show(); s_lastRotate = millisNow(); break;
            case SDLK_a:      cambiarApp(); break;
            case SDLK_l:      mostrarLauncher(); break;
            case SDLK_c:      mostrarConfig(); break;
            case SDLK_p:      s_paused = !s_paused;
                              std::printf("  rotacion %s\n", s_paused ? "en pausa" : "activa");
                              break;
            case SDLK_t:      simSetTimingEnabled(!simTimingEnabled());
                              simResetStats();
                              std::printf("  modelo de tiempo del hardware: %s\n",
                                          simTimingEnabled() ? "ON" : "OFF");
                              break;
            case SDLK_f:      report(); break;
            case SDLK_ESCAPE: std::exit(0);
            default: break;
        }
    }

    if (s_estatica) { SDL_Delay(8); return; }

    const uint32_t now = millisNow();
    const uint32_t since = now - s_lastRotate;
    if (!s_paused && since >= s_rotateMs) advance(+1);

    const float progreso = (float)since / (float)s_rotateMs;
    if (s_app == APP_PADEL) uiPadelTick(progreso);
    else                    uiFercedTick(nowEpoch(), progreso);
    SDL_Delay(1);
}

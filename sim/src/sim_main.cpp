// Entrada del simulador. Maneja la misma maquina de rotacion que el firmware
// para que el ritmo sea el mismo, y agrega atajos de teclado para iterar.

#include "sim_display.h"
#include "apps.h"
#include "feed_client.h"
#include "padel_client.h"
#include "ui_config.h"
#include "ui_ferced.h"
#include "ui_launcher.h"
#include "ui_padel.h"

#include <SDL2/SDL.h>

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
        "1.1.0", "e96efeb-dirty", "2026-08-05 04:20",
        "MiWiFi", "192.168.1.41", "feed.ferced.com",
        8073, 35.4f, 20, true
    };
    s_estatica = true;
    uiConfigDraw(demo);

    // El mismo llamado que hace el callback de progreso del OTA en el firmware,
    // con la version falsa: aca no se descarga nada, se mira como queda.
    if (g_progreso >= 0) uiConfigEstado("Descargando 1.1.1", g_progreso);
}

// Igual que llenarApps() del firmware, con los mismos datos que tendria arriba.
static void mostrarLauncher() {
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

    s_estatica = true;
    uiLauncherDraw(apps, APP_COUNT, s_app);
}

static void show() {
    s_estatica = false;
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

static void cambiarApp() {
    s_app = (uint8_t)((s_app + 1) % APP_COUNT);
    std::printf("  app: %s\n", s_app == APP_PADEL ? "padel" : "noticias");
    show();
    s_lastRotate = millisNow();
}

static void applyArgs() {
    if (g_padel) s_app = APP_PADEL;
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
    banner();

    feedFetch();
    padelFetch();
    applyArgs();
    show();
    s_lastRotate = millisNow();
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

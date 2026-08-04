// Entrada del simulador. Maneja la misma maquina de rotacion que el firmware
// para que el ritmo sea el mismo, y agrega atajos de teclado para iterar.

#include "sim_display.h"
#include "feed_client.h"
#include "ui_ferced.h"
#include "ui_config.h"

#include <SDL2/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

static uint8_t  s_index = 0;
static uint32_t s_lastRotate = 0;
static bool     s_paused = false;
static uint32_t s_rotateMs = 17000;
// Mientras la pantalla de configuracion esta arriba hay que frenar el tick del
// feed: si no, la animacion repinta encima y la captura sale con el titular.
static bool     s_config = false;

// Configuracion por linea de comandos. El teclado no sirve para automatizar:
// Panel_sdl corre su propio bucle de eventos en el hilo principal y consume
// las teclas antes de que las vea este loop.
int      g_startIndex = 0;
bool     g_still      = false;   // sin rotacion, para capturas estables
bool     g_config     = false;   // arrancar en la pantalla de configuracion
int      g_progreso   = -1;      // 0..100: congela la franja de estado del OTA

static void applyArgs() {
    if (g_startIndex > 0) {
        const uint8_t total = feedCount();
        if (total > 0) s_index = (uint8_t)(g_startIndex % total);
    }
    if (g_still) s_paused = true;
}

// Datos falsos: lo que se verifica aca es la disposicion, no los valores.
static void mostrarConfig() {
    static const ConfigInfo demo = {
        "1.0.0", "e96efeb-dirty", "2026-08-03 23:51",
        "MiWiFi", "192.168.1.41", "feed.ferced.com",
        8073, 35.4f, 20, true
    };
    s_config = true;
    uiConfigDraw(demo);

    // El mismo llamado que hace el callback de progreso del OTA en el firmware,
    // con la version falsa: aca no se descarga nada, se mira como queda.
    if (g_progreso >= 0) uiConfigEstado("Descargando 1.0.1", g_progreso);
}

// El firmware usa millis(); aca lo replico sobre el reloj del sistema.
static uint32_t millisNow() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

static uint32_t nowEpoch() { return (uint32_t)time(nullptr); }

static void show() {
    const uint8_t total = feedCount();
    if (total == 0) {
        uiFercedShowStatus("Sin contenido", "El fixture esta vacio. Corre tools/fetch_fixture.py.");
        return;
    }
    if (s_index >= total) s_index = 0;
    uiFercedShowItem(feedItem(s_index), s_index, total, false);
}

static void advance(int delta) {
    const uint8_t total = feedCount();
    if (total == 0) return;
    s_index = (uint8_t)((s_index + total + delta) % total);
    show();
    s_lastRotate = millisNow();
}

static void banner() {
    std::printf(
        "\n  ferced-display  simulador\n"
        "  ---------------------------------------------\n"
        "  ESPACIO  siguiente item        P  pausar rotacion\n"
        "  B        item anterior         R  repetir animacion\n"
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
    banner();

    feedFetch();
    applyArgs();
    show();
    s_lastRotate = millisNow();
    if (g_config) mostrarConfig();
}

void loop() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type != SDL_KEYDOWN) continue;
        switch (e.key.keysym.sym) {
            case SDLK_SPACE:  s_config = false; advance(+1); break;
            case SDLK_b:      s_config = false; advance(-1); break;
            case SDLK_r:      s_config = false; show(); s_lastRotate = millisNow(); break;
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

    if (s_config) { SDL_Delay(8); return; }

    const uint32_t now = millisNow();
    const uint32_t since = now - s_lastRotate;
    if (!s_paused && since >= s_rotateMs) advance(+1);

    uiFercedTick(nowEpoch(), (float)since / (float)s_rotateMs);
    SDL_Delay(1);
}

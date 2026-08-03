// Entrada del simulador. Maneja la misma maquina de rotacion que el firmware
// para que el ritmo sea el mismo, y agrega atajos de teclado para iterar.

#include "sim_display.h"
#include "feed_client.h"
#include "ui_ferced.h"

#include <SDL2/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

static uint8_t  s_index = 0;
static uint32_t s_lastRotate = 0;
static bool     s_paused = false;
static uint32_t s_rotateMs = 17000;

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
    show();
    s_lastRotate = millisNow();
}

void loop() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type != SDL_KEYDOWN) continue;
        switch (e.key.keysym.sym) {
            case SDLK_SPACE:  advance(+1); break;
            case SDLK_b:      advance(-1); break;
            case SDLK_r:      show(); s_lastRotate = millisNow(); break;
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

    const uint32_t now = millisNow();
    const uint32_t since = now - s_lastRotate;
    if (!s_paused && since >= s_rotateMs) advance(+1);

    uiFercedTick(nowEpoch(), (float)since / (float)s_rotateMs);
    SDL_Delay(1);
}

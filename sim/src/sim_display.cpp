// Reemplazo de display_manager para el simulador.
//
// El objetivo no es "parecerse": es reproducir las dos cosas que definen como
// se ve la UI en la caja. Los pixeles los da LovyanGFX, que es la misma
// biblioteca que corre en el ESP32. El tiempo lo da el modelo de abajo.

#include "sim_display.h"
#include "config.h"

#include <chrono>
#include <thread>

LGFX tft;

// ── Modelo de tiempo, calibrado contra el aparato ──
//
// Mediciones reales del ESP32-S3 (telemetria enviada por el firmware):
//   pantalla completa (480 filas) -> 98,8 ms por frame
//   banda de ~80 filas            -> 36,0 ms por frame
//
// Ajuste lineal: costo(ms) = 23,4 + 0,1575 * filas
// El termino constante coincide con el periodo de VSync, que es lo esperable.
//
// VSync del panel: pclk 12 MHz sobre 548x518 con porches = 42,3 Hz = 23,64 ms.
// Un frame que se pasa de un periodo espera al siguiente, y ahi el framerate
// cae de golpe a la mitad. Sin simular esa cuantizacion, las animaciones se
// verian fluidas aca y seguirian tironeando en la caja.

static constexpr double VSYNC_MS      = 23.64;
static constexpr double COST_PER_ROW  = 0.1575;
static constexpr double COST_FIXED_MS = 0.0;   // el fijo ES la espera de VSync

static double s_pushedRows = 0;
static bool   s_timingOn   = true;
static SimStats s_stats;

using clk = std::chrono::steady_clock;
static clk::time_point s_lastVsync = clk::now();

void simSetTimingEnabled(bool on) { s_timingOn = on; }
bool simTimingEnabled() { return s_timingOn; }
SimStats simStats() { return s_stats; }
void simResetStats() { s_stats = SimStats{}; }

void displaySetup() {
    tft.init();
    tft.setColorDepth(16);
}

void displaySetBrightness(uint8_t) {}

void displaySetupVSync() { s_lastVsync = clk::now(); }

// Cuenta el trabajo de composicion del frame y espera al proximo VSync,
// exactamente como hace el panel real.
void displayWaitVSync() {
    if (!s_timingOn) return;

    const double workMs = COST_FIXED_MS + COST_PER_ROW * s_pushedRows;
    s_pushedRows = 0;

    if (workMs > 0.0) {
        std::this_thread::sleep_for(
            std::chrono::microseconds((long long)(workMs * 1000.0)));
    }

    // Cuantizacion: se despierta recien en el proximo borde de VSync.
    const auto now = clk::now();
    double since = std::chrono::duration<double, std::milli>(now - s_lastVsync).count();
    double periods = 1.0;
    while (since > VSYNC_MS * periods) periods += 1.0;
    const double waitMs = VSYNC_MS * periods - since;
    if (waitMs > 0.0) {
        std::this_thread::sleep_for(
            std::chrono::microseconds((long long)(waitMs * 1000.0)));
    }
    s_lastVsync = clk::now();

    s_stats.frames++;
    s_stats.lastFrameMs = workMs + waitMs;
    s_stats.sumMs += s_stats.lastFrameMs;
    if (s_stats.lastFrameMs > s_stats.worstMs) s_stats.worstMs = s_stats.lastFrameMs;
}

// El firmware llama a esto con los bytes empujados; de ahi salen las filas.
void displayRecordPush(uint32_t bytes, uint32_t) {
    s_pushedRows = bytes / (double)(SCREEN_W * 2);
}

DisplayDiagnostics displayGetDiagnostics() { return DisplayDiagnostics{}; }

#pragma once

// El simulador usa el display_manager.h REAL del firmware, que bajo
// -DFERCED_SIM incluye el panel SDL de LovyanGFX. La cuantizacion RGB565 y el
// antialiasing de las primitivas son identicos porque es la misma biblioteca.

#include "display_manager.h"

#include <cstdint>

// ── Control del modelo de tiempo ──
struct SimStats {
    uint32_t frames = 0;
    double   sumMs = 0;
    double   worstMs = 0;
    double   lastFrameMs = 0;
};

// Apagarlo muestra la animacion "ideal", sin el techo del hardware. Sirve para
// ver que se pierde, pero lo que hay que juzgar es con el modelo prendido.
void simSetTimingEnabled(bool on);
bool simTimingEnabled();
SimStats simStats();
void simResetStats();

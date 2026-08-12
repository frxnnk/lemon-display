#pragma once

// Panel SDL de LovyanGFX con la misma geometria y profundidad que la caja.
// La cuantizacion de color RGB565 y el antialiasing de las primitivas son
// identicos porque es la misma biblioteca, no una imitacion.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_sdl _panel;

public:
    LGFX() {
        auto cfg = _panel.config();
        cfg.memory_width  = 480;
        cfg.memory_height = 480;
        cfg.panel_width   = 480;
        cfg.panel_height  = 480;
        _panel.config(cfg);
        _panel.setScaling(1, 1);
        _panel.setWindowTitle("ferced-display  ·  simulador");
        setPanel(&_panel);
    }
};

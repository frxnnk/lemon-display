#pragma once

#include <cstdint>
#include <cmath>

// ══════════════════════════════════════════════
//  Lemon Brand — Official Bitmaps (RGB565)
// ══════════════════════════════════════════════

// Isotipo only (coin icon)
#include "lemon_isotipo_64.h"
#include "lemon_isotipo_28.h"

// Full imagotipo (coin + LEMON wordmark)
#include "lemon_imagotipo_244.h"
#include "lemon_imagotipo_122.h"

// ── Pixel-by-pixel render functions ──
// Works reliably with any rotation (pushImage has issues on rotated RGB panels)

template <typename T>
inline void drawLemonIsotipo64(T& gfx, int x, int y) {
    for (int py = 0; py < 64; py++) {
        for (int px = 0; px < 64; px++) {
            uint16_t c = lemon_isotipo_64[py * 64 + px];
            if (c != 0x0000) gfx.drawPixel(x + px, y + py, c);
        }
    }
}

template <typename T>
inline void drawLemonIsotipo28(T& gfx, int x, int y) {
    for (int py = 0; py < 28; py++) {
        for (int px = 0; px < 28; px++) {
            uint16_t c = lemon_isotipo_28[py * 28 + px];
            if (c != 0x0000) gfx.drawPixel(x + px, y + py, c);
        }
    }
}

template <typename T>
inline void drawLemonImagotipo244(T& gfx, int x, int y) {
    for (int py = 0; py < 56; py++) {
        for (int px = 0; px < 244; px++) {
            uint16_t c = lemon_imagotipo_244[py * 244 + px];
            if (c != 0x0000) gfx.drawPixel(x + px, y + py, c);
        }
    }
}

template <typename T>
inline void drawLemonImagotipo122(T& gfx, int x, int y) {
    for (int py = 0; py < 28; py++) {
        for (int px = 0; px < 122; px++) {
            uint16_t c = lemon_imagotipo_122[py * 122 + px];
            if (c != 0x0000) gfx.drawPixel(x + px, y + py, c);
        }
    }
}

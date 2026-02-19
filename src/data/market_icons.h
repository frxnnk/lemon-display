#pragma once

#include <cstdint>

#include "btc_logo_28.h"
#include "btc_logo_20.h"
#include "usd_logo_28.h"
#include "usd_logo_20.h"

template <typename T>
inline void drawBitmapTransparent(T& gfx, int x, int y,
                                  const uint16_t* data, int w, int h) {
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            uint16_t c = data[py * w + px];
            if (c != 0x0000) gfx.drawPixel(x + px, y + py, c);
        }
    }
}

template <typename T>
inline void drawBtcLogo28(T& gfx, int x, int y) {
    drawBitmapTransparent(gfx, x, y, btc_logo_28, 28, 28);
}

template <typename T>
inline void drawBtcLogo20(T& gfx, int x, int y) {
    drawBitmapTransparent(gfx, x, y, btc_logo_20, 20, 20);
}

template <typename T>
inline void drawUsdLogo28(T& gfx, int x, int y) {
    drawBitmapTransparent(gfx, x, y, usd_logo_28, 28, 28);
}

template <typename T>
inline void drawUsdLogo20(T& gfx, int x, int y) {
    drawBitmapTransparent(gfx, x, y, usd_logo_20, 20, 20);
}

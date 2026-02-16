#pragma once

#include <cstdint>
#include <cstdlib>

// ── Simple hit-test helpers ──

inline bool touchInRect(int16_t tx, int16_t ty, int rx, int ry, int rw, int rh) {
    return tx >= rx && tx < (rx + rw) && ty >= ry && ty < (ry + rh);
}

inline bool touchInCircle(int16_t tx, int16_t ty, int cx, int cy, int r) {
    int dx = tx - cx;
    int dy = ty - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

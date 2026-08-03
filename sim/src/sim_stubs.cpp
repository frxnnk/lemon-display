// Piezas del firmware que el simulador no necesita, con la misma firma para
// que ui_ferced.cpp compile sin tocarse. Es importante que ui_ferced sea el
// archivo REAL y no una copia: una copia se desincroniza y el simulador deja
// de decir la verdad.

#include <cstdint>

// ── design_system.h necesita las fuentes Satoshi, que vienen del firmware ──
// No hay stub aca: se compilan las mismas src/data/satoshi_fonts.h.

// ── touch ──
enum TouchGesture : uint8_t {
    TOUCH_NONE, TOUCH_TAP, TOUCH_LONG_PRESS,
    TOUCH_SWIPE_LEFT, TOUCH_SWIPE_RIGHT, TOUCH_SWIPE_UP, TOUCH_SWIPE_DOWN,
    TOUCH_FLING_UP, TOUCH_FLING_DOWN, TOUCH_PINCH, TOUCH_DOUBLE_TAP
};

struct TouchEvent {
    int16_t x, y;
    TouchGesture gesture;
    float velocityY, pinchScale;
    int16_t pinchCenterX, pinchCenterY;
};

void touchSetup() {}
TouchEvent touchLoop() { return TouchEvent{0, 0, TOUCH_NONE, 0, 0, 0, 0}; }

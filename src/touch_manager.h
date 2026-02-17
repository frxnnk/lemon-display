#pragma once

#include <cstdint>

enum TouchGesture : uint8_t {
    TOUCH_NONE,
    TOUCH_TAP,
    TOUCH_LONG_PRESS,
    TOUCH_SWIPE_LEFT,
    TOUCH_SWIPE_RIGHT,
    TOUCH_SWIPE_UP,
    TOUCH_SWIPE_DOWN
};

struct TouchEvent {
    int16_t x;
    int16_t y;
    TouchGesture gesture;
};

void touchSetup();
TouchEvent touchLoop();  // Poll every frame, returns event (TOUCH_NONE if nothing)

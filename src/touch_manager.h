#pragma once

#include <cstdint>

enum TouchGesture : uint8_t {
    TOUCH_NONE,
    TOUCH_TAP,
    TOUCH_LONG_PRESS,
    TOUCH_SWIPE_LEFT,
    TOUCH_SWIPE_RIGHT,
    TOUCH_SWIPE_UP,
    TOUCH_SWIPE_DOWN,
    TOUCH_FLING_UP,
    TOUCH_FLING_DOWN,
    TOUCH_PINCH,
    TOUCH_DOUBLE_TAP
};

struct TouchEvent {
    int16_t x;
    int16_t y;
    TouchGesture gesture;
    float velocityY;     // px/s at release (positive = downward)
    float pinchScale;    // ratio: currentDist / startDist (for TOUCH_PINCH)
    int16_t pinchCenterX;
    int16_t pinchCenterY;
};

void touchSetup();
TouchEvent touchLoop();  // Poll every frame, returns event (TOUCH_NONE if nothing)

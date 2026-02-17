#include "touch_manager.h"
#include "display_manager.h"
#include <Arduino.h>

// ── Touch state ──
static bool     wasTouching    = false;
static int16_t  touchStartX    = 0;
static int16_t  touchStartY    = 0;
static int16_t  lastValidX     = 0;
static int16_t  lastValidY     = 0;
static uint32_t touchStartTime = 0;
static uint32_t lastTapTime    = 0;

// ── Tuning ──
static const uint32_t DEBOUNCE_MS    = 200;  // Min time between taps
static const uint32_t LONG_PRESS_MS  = 500;  // Long press threshold
static const int16_t  SWIPE_MIN_PX   = 60;   // Min horizontal movement for swipe
static const uint32_t SWIPE_MAX_MS   = 300;  // Max time for a swipe gesture

void touchSetup() {
    Serial.println("[Touch] GT911 ready via LovyanGFX");
}

TouchEvent touchLoop() {
    TouchEvent evt = { 0, 0, TOUCH_NONE };

    lgfx::touch_point_t tp;
    int touchCount = tft.getTouch(&tp, 1);
    bool isTouching = (touchCount > 0);
    uint32_t now = millis();

    if (isTouching) {
        // Always track last valid touch position
        lastValidX = tp.x;
        lastValidY = tp.y;
    }

    if (isTouching && !wasTouching) {
        // Touch down
        touchStartX = tp.x;
        touchStartY = tp.y;
        touchStartTime = now;
        wasTouching = true;
    } else if (!isTouching && wasTouching) {
        // Touch up — determine gesture
        // NOTE: tp.x/y are INVALID here (GT911 returns garbage on touch-up)
        // Use lastValidX/Y which was the last position while finger was down
        wasTouching = false;

        uint32_t duration = now - touchStartTime;
        int16_t dx = lastValidX - touchStartX;
        int16_t dy = lastValidY - touchStartY;

        // Debounce
        if (now - lastTapTime < DEBOUNCE_MS) {
            return evt;
        }

        evt.x = touchStartX;
        evt.y = touchStartY;

        int16_t absDx = abs(dx);
        int16_t absDy = abs(dy);

        if (duration >= LONG_PRESS_MS && absDx < SWIPE_MIN_PX && absDy < SWIPE_MIN_PX) {
            evt.gesture = TOUCH_LONG_PRESS;
        } else if (duration <= SWIPE_MAX_MS && (absDx >= SWIPE_MIN_PX || absDy >= SWIPE_MIN_PX)) {
            // Dominant-axis swipe detection
            if (absDx >= absDy) {
                // Horizontal swipe
                evt.gesture = (dx > 0) ? TOUCH_SWIPE_RIGHT : TOUCH_SWIPE_LEFT;
            } else {
                // Vertical swipe (UP = finger moves up = dy < 0)
                evt.gesture = (dy < 0) ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;
            }
        } else if (duration < LONG_PRESS_MS && absDx < SWIPE_MIN_PX && absDy < SWIPE_MIN_PX) {
            evt.gesture = TOUCH_TAP;
        }

        if (evt.gesture != TOUCH_NONE) {
            lastTapTime = now;
            static const char* gestureNames[] = {
                "NONE", "TAP", "LONG_PRESS", "SWIPE_LEFT", "SWIPE_RIGHT", "SWIPE_UP", "SWIPE_DOWN"
            };
            Serial.printf("[Touch] %s at (%d,%d)\n",
                          gestureNames[evt.gesture], evt.x, evt.y);
        }
    }

    return evt;
}

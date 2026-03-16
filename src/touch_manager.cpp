#include "touch_manager.h"
#include "display_manager.h"
#include "config.h"
#include <Arduino.h>
#include <cmath>

// ── Single-touch state ──
static bool     wasTouching    = false;
static int16_t  touchStartX    = 0;
static int16_t  touchStartY    = 0;
static int16_t  lastValidX     = 0;
static int16_t  lastValidY     = 0;
static uint32_t touchStartTime = 0;
static uint32_t lastTapTime    = 0;
static int16_t  lastTapX       = 0;
static int16_t  lastTapY       = 0;

// ── Velocity tracking (ring buffer of last 4 positions) ──
#define VEL_SAMPLES 4
static int16_t  velY[VEL_SAMPLES];
static uint32_t velTime[VEL_SAMPLES];
static uint8_t  velIdx   = 0;
static uint8_t  velCount = 0;

// ── Multi-touch / pinch state ──
static bool     pinchActive    = false;
static float    pinchStartDist = 0.0f;
static float    pinchLastScale = 1.0f;
static int16_t  pinchCX        = 0;
static int16_t  pinchCY        = 0;

// ── Tuning ──
static const uint32_t DEBOUNCE_MS       = 120;
static const uint32_t LONG_PRESS_MS     = 500;
static const int16_t  SWIPE_MIN_PX      = 60;
static const uint32_t SWIPE_MAX_MS      = 300;
static const float    FLING_MIN_VEL     = 400.0f;
static const uint32_t FLING_MAX_MS      = 300;
static const uint32_t DOUBLE_TAP_MAX_MS = 300;
static const int16_t  DOUBLE_TAP_MAX_PX = 30;

static float dist2d(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    return sqrtf(dx * dx + dy * dy);
}

void touchSetup() {
    Serial.println("[Touch] GT911 ready via LovyanGFX (multi-touch enabled)");
}

TouchEvent touchLoop() {
    TouchEvent evt = { 0, 0, TOUCH_NONE, 0.0f, 1.0f, 0, 0 };

    // Read up to 5 touch points from GT911
    lgfx::touch_point_t tp[5];
    int touchCount = tft.getTouch(tp, 5);
    bool isTouching = (touchCount > 0);
    uint32_t now = millis();

    // ── Multi-touch: pinch detection ──
    if (touchCount >= 2) {
        float curDist = dist2d(tp[0].x, tp[0].y, tp[1].x, tp[1].y);
        int16_t cx = (tp[0].x + tp[1].x) / 2;
        int16_t cy = (tp[0].y + tp[1].y) / 2;

        if (!pinchActive) {
            // Start pinch
            pinchActive = true;
            pinchStartDist = curDist;
            if (pinchStartDist < 10.0f) pinchStartDist = 10.0f;
            pinchLastScale = 1.0f;
            pinchCX = cx;
            pinchCY = cy;
        } else {
            // Ongoing pinch — emit event
            float scale = curDist / pinchStartDist;
            if (fabsf(scale - pinchLastScale) > 0.02f) {
                pinchLastScale = scale;
                pinchCX = cx;
                pinchCY = cy;
                evt.gesture = TOUCH_PINCH;
                evt.pinchScale = scale;
                evt.pinchCenterX = cx;
                evt.pinchCenterY = cy;
                evt.x = cx;
                evt.y = cy;
                return evt;
            }
        }

        // While pinching, don't process single-touch logic
        lastValidX = tp[0].x;
        lastValidY = tp[0].y;
        wasTouching = true;
        touchStartTime = now;  // Reset to prevent stale gesture on release
        return evt;
    }

    // If pinch just ended (was active, now single or no touch)
    if (pinchActive && touchCount < 2) {
        pinchActive = false;
        // Suppress any gesture on release after pinch
        if (!isTouching) {
            wasTouching = false;
        }
        return evt;
    }

    // ── Single-touch logic ──
    if (isTouching) {
        lastValidX = tp[0].x;
        lastValidY = tp[0].y;

        // Clamp to screen bounds (GT911 can report out-of-range values)
        if (lastValidX < 0) lastValidX = 0;
        if (lastValidX >= SCREEN_W) lastValidX = SCREEN_W - 1;
        if (lastValidY < 0) lastValidY = 0;
        if (lastValidY >= SCREEN_H) lastValidY = SCREEN_H - 1;

        // Record position for velocity calculation
        velY[velIdx]    = lastValidY;
        velTime[velIdx] = now;
        velIdx = (velIdx + 1) % VEL_SAMPLES;
        if (velCount < VEL_SAMPLES) velCount++;
    }

    if (isTouching && !wasTouching) {
        // Touch down
        touchStartX = lastValidX;
        touchStartY = lastValidY;
        touchStartTime = now;
        wasTouching = true;
        velCount = 0;
        velIdx = 0;
    } else if (!isTouching && wasTouching) {
        // Touch up — determine gesture
        wasTouching = false;

        uint32_t duration = now - touchStartTime;
        int16_t dx = lastValidX - touchStartX;
        int16_t dy = lastValidY - touchStartY;

        // Compute velocity from ring buffer
        float velocityY = 0.0f;
        if (velCount >= 2) {
            uint8_t newest = (velIdx + VEL_SAMPLES - 1) % VEL_SAMPLES;
            uint8_t oldest = (velIdx + VEL_SAMPLES - velCount) % VEL_SAMPLES;
            int32_t dtMs = (int32_t)(velTime[newest] - velTime[oldest]);
            if (dtMs > 0) {
                velocityY = (float)(velY[newest] - velY[oldest]) / ((float)dtMs / 1000.0f);
            }
        }

        // Debounce
        if (now - lastTapTime < DEBOUNCE_MS) {
            return evt;
        }

        evt.x = touchStartX;
        evt.y = touchStartY;
        evt.velocityY = velocityY;

        int16_t absDx = abs(dx);
        int16_t absDy = abs(dy);

        // Check for fling (fast vertical gesture)
        if (duration <= FLING_MAX_MS && fabsf(velocityY) >= FLING_MIN_VEL && absDy > 20) {
            evt.gesture = (velocityY < 0) ? TOUCH_FLING_UP : TOUCH_FLING_DOWN;
        }
        else if (duration >= LONG_PRESS_MS && absDx < SWIPE_MIN_PX && absDy < SWIPE_MIN_PX) {
            evt.gesture = TOUCH_LONG_PRESS;
        } else if (duration <= SWIPE_MAX_MS && (absDx >= SWIPE_MIN_PX || absDy >= SWIPE_MIN_PX)) {
            if (absDx >= absDy) {
                evt.gesture = (dx > 0) ? TOUCH_SWIPE_RIGHT : TOUCH_SWIPE_LEFT;
            } else {
                evt.gesture = (dy < 0) ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;
            }
        } else if (duration < LONG_PRESS_MS && absDx < SWIPE_MIN_PX && absDy < SWIPE_MIN_PX) {
            // Check for double-tap
            if ((now - lastTapTime) < DOUBLE_TAP_MAX_MS &&
                abs(touchStartX - lastTapX) < DOUBLE_TAP_MAX_PX &&
                abs(touchStartY - lastTapY) < DOUBLE_TAP_MAX_PX) {
                evt.gesture = TOUCH_DOUBLE_TAP;
            } else {
                evt.gesture = TOUCH_TAP;
            }
        }

        if (evt.gesture != TOUCH_NONE) {
            lastTapTime = now;
            lastTapX = touchStartX;
            lastTapY = touchStartY;
            static const char* gestureNames[] = {
                "NONE", "TAP", "LONG_PRESS", "SWIPE_LEFT", "SWIPE_RIGHT",
                "SWIPE_UP", "SWIPE_DOWN", "FLING_UP", "FLING_DOWN",
                "PINCH", "DOUBLE_TAP"
            };
            Serial.printf("[Touch] %s at (%d,%d) vel=%.0f\n",
                          gestureNames[evt.gesture], evt.x, evt.y, velocityY);
        }
    }

    return evt;
}

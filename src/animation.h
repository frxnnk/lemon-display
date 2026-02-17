#pragma once

#include <Arduino.h>
#include "data_models.h"

// ══════════════════════════════════════════
//  ValueAnimator — smooth price counting transitions
//  Uses ease-out cubic: 1 - (1-t)^3
// ══════════════════════════════════════════

class ValueAnimator {
    float current_;
    float target_;
    float startVal_;
    unsigned long startMs_;
    uint16_t durationMs_;
    bool animating_;

public:
    ValueAnimator() : current_(0), target_(0), startVal_(0),
                      startMs_(0), durationMs_(500), animating_(false) {}

    void setTarget(float newTarget, uint16_t ms = 500) {
        if (newTarget == target_ && !animating_) {
            // Same target, no animation needed
            return;
        }
        startVal_ = current_;
        target_ = newTarget;
        startMs_ = millis();
        durationMs_ = ms;
        animating_ = true;
    }

    // Force-set without animation (for initial values)
    void set(float val) {
        current_ = val;
        target_ = val;
        startVal_ = val;
        animating_ = false;
    }

    float update() {
        if (!animating_) return current_;

        unsigned long elapsed = millis() - startMs_;
        if (elapsed >= durationMs_) {
            current_ = target_;
            animating_ = false;
            return current_;
        }

        float t = (float)elapsed / durationMs_;
        // Ease-out cubic: 1 - (1-t)^3
        float inv = 1.0f - t;
        float ease = 1.0f - (inv * inv * inv);
        current_ = startVal_ + (target_ - startVal_) * ease;
        return current_;
    }

    float value() const { return current_; }
    bool isAnimating() const { return animating_; }
};

// ══════════════════════════════════════════
//  SparklineAnimator — progressive sparkline reveal
//  visibleCount increases from 0 to data.count over durationMs
// ══════════════════════════════════════════

class SparklineAnimator {
    unsigned long startMs_;
    uint16_t durationMs_;
    uint16_t totalCount_;
    bool animating_;

public:
    SparklineAnimator() : startMs_(0), durationMs_(800),
                          totalCount_(0), animating_(false) {}

    void start(uint16_t count, uint16_t ms = 800) {
        totalCount_ = count;
        startMs_ = millis();
        durationMs_ = ms;
        animating_ = true;
    }

    uint16_t visibleCount() {
        if (!animating_) return totalCount_;

        unsigned long elapsed = millis() - startMs_;
        if (elapsed >= durationMs_) {
            animating_ = false;
            return totalCount_;
        }

        float t = (float)elapsed / durationMs_;
        // Ease-out quad for smooth reveal
        float ease = 1.0f - (1.0f - t) * (1.0f - t);
        uint16_t visible = (uint16_t)(ease * totalCount_);
        return visible < 1 ? 1 : visible;
    }

    bool isAnimating() const { return animating_; }
};

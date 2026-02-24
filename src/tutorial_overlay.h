#pragma once

#include <cstdint>
#include "touch_manager.h"

// Tutorial overlay — interactive spotlight walkthrough (first boot only)
void tutorialInit();           // Check NVS, activate if first boot
void tutorialStart();          // Force-start tutorial (from settings)
void tutorialStartPro();       // Queue pro mini-tutorial (starts on dashboard entry)
void tutorialCheckPending();   // Call from dashboard loop — starts queued pro tutorial
bool tutorialIsActive();       // True while tutorial is running
bool tutorialNeedsReset();     // True once when tutorial starts (reset period + redraw)
bool tutorialNeedsDraw();      // True when step changed (consumes flag)
void tutorialDraw();           // Render dimming + spotlight + tooltip on tft
void tutorialHandleTouch(const TouchEvent& evt);  // Intercept all touch

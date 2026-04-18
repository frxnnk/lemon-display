#pragma once

#include <cstdint>
#include "touch_manager.h"

// Top-level dashboard views. The user swipes horizontally between them at
// the top level; each view owns its own drawing + touch handling.
// v5.0 adds Stocks; Polymarket extraction comes in v5.2 (for now it remains
// an overlay inside VIEW_CRYPTO).
enum ViewId : uint8_t {
    VIEW_CRYPTO = 0,
    VIEW_STOCKS,
    VIEW_COUNT        // keep last
};

typedef void (*ViewRedrawCB)();

void   viewsInit();
ViewId viewsGetCurrent();
void   viewsSetCurrent(ViewId v);
void   viewsCycleNext();   // temporary: advance to next view (wraps)
uint8_t viewsCount();

// Each view registers its own full-redraw callback during setup.
void viewsRegisterRedraw(ViewId v, ViewRedrawCB cb);

// Draw the dots indicator at the bottom of the screen. Views call this at
// the end of their redraw so it composites on top of their content.
void viewsDrawDotsOverlay();

// Called by app_state when SCREEN_DASHBOARD is active.
void viewsHandleTouch(const TouchEvent& evt);
void viewsTick();
void viewsDraw();                 // full redraw of the current view
void viewsFillGaps();             // used when leaving Settings to clear remnants

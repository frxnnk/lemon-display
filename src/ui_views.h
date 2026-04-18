#pragma once

#include <cstdint>
#include "touch_manager.h"

// Top-level dashboard views. Currently a single-view shim around the existing
// ui_dashboard; v5.0 will grow to include VIEW_STOCKS and VIEW_POLYMARKET so
// the user can swipe horizontally between them.
enum ViewId : uint8_t {
    VIEW_CRYPTO = 0,
    VIEW_COUNT        // keep last
};

typedef void (*ViewRedrawCB)();

void   viewsInit();
ViewId viewsGetCurrent();
void   viewsSetCurrent(ViewId v);
uint8_t viewsCount();

// Each view registers its own full-redraw callback during setup.
void viewsRegisterRedraw(ViewId v, ViewRedrawCB cb);

// Called by app_state when SCREEN_DASHBOARD is active.
void viewsHandleTouch(const TouchEvent& evt);
void viewsTick();
void viewsDraw();                 // full redraw of the current view
void viewsFillGaps();             // used when leaving Settings to clear remnants

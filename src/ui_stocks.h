#pragma once

#include "touch_manager.h"

// One-time init: loads the watchlist from NVS.
void stocksInit();

// Scheduler task body: refreshes quotes + focused-symbol chart.
// Safe to call at any time (non-blocking, single-shot).
void stocksFetchTask();

// Force a redraw next time the Stocks view is active (e.g. after watchlist
// edit via captive portal).
void stocksMarkDirty();

// Wired through ui_views.
void stocksDrawAll();
void stocksHandleTouch(const TouchEvent& evt);
void stocksTick();

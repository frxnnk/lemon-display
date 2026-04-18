#pragma once

#include "touch_manager.h"

// v5.0 step 2: placeholder shell. Actual Yahoo Finance integration +
// watchlist rendering lands in a follow-up commit.
void stocksDrawAll();
void stocksHandleTouch(const TouchEvent& evt);
void stocksTick();

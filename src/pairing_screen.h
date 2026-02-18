#pragma once

#include "touch_manager.h"

// Draw pairing screen (code can be nullptr to use cached code from supabase client)
void pairingScreenDraw(const char* code);

// Handle touch events on pairing screen
void pairingScreenHandleTouch(const TouchEvent& evt);

// Per-frame tick (pulsing dot animation, auto-skip timer, pairing detection)
void pairingScreenTick();

#pragma once

#include "lgfx_matouch_40.h"

extern LGFX tft;

void displaySetup();
void displaySetBrightness(uint8_t level); // 0-255 (no-op on MaTouch, always-on backlight)

// VSync synchronization for RGB panel — call after displaySetup()
void displaySetupVSync();
void displayWaitVSync();

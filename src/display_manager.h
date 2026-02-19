#pragma once

#include "lgfx_matouch_40.h"

extern LGFX tft;

void displaySetup();
void displaySetBrightness(uint8_t level); // 0-255

// VSync synchronization for RGB panel — call after displaySetup()
void displaySetupVSync();
void displayWaitVSync();

#pragma once

#include "lgfx_matouch_40.h"

extern LGFX tft;

struct DisplayDiagnostics {
    uint32_t vsyncCount;
    uint32_t waitCalls;
    uint32_t waitTimeouts;
    uint32_t pushCount;
    uint64_t pushedBytes;
    uint32_t lastPushUs;
    uint32_t maxPushUs;
    uint32_t lastPushBytes;
    uint32_t maxPushBytes;
};

void displaySetup();
void displaySetBrightness(uint8_t level); // 0-255

// VSync synchronization for RGB panel — call after displaySetup()
void displaySetupVSync();
void displayWaitVSync();
void displayRecordPush(uint32_t bytes, uint32_t durationUs);
DisplayDiagnostics displayGetDiagnostics();

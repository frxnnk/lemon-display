#pragma once

// El simulador de escritorio (sim/) compila este mismo header y el mismo
// ui_ferced.cpp, pero con el panel SDL de LovyanGFX en vez del RGB del
// ESP32. Es un solo condicional para que no existan dos copias de la UI:
// una copia se desincroniza y el simulador deja de decir la verdad.
#if defined(FERCED_SIM)
  #include "sim_panel.h"
#else
  #include "lgfx_matouch_40.h"
#endif

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

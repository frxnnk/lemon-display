#pragma once
#include <Arduino.h>
#include "display_manager.h"
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>

// ── Double-buffer helpers ──
// STATUS: NOT FUNCTIONAL. The LovyanGFX Bus_RGB class does not implement
// getBackBuffer(), getDisplayBuffer(), requestSwap(), swapPending(), or
// cancelSwap(). These require a custom patch to Bus_RGB.cpp/hpp.
//
// The single-buffer deferred-push architecture (dashboardSetDeferred /
// dashboardFlushDeferred in ui_dashboard) provides effective tearing
// mitigation by batching all VSync waits into one per frame.
//
// To enable double buffering in the future:
// 1. Patch Bus_RGB to allocate a second PSRAM buffer
// 2. Implement swap in the ISR (swap DMA descriptor base on VSYNC_END)
// 3. Add getBackBuffer/getDisplayBuffer/requestSwap/swapPending/cancelSwap
// 4. Add Panel_FrameBufferBase::rebindFrameBuffer(uint8_t* fb)
// 5. Uncomment dbuf::enable() call in main.cpp enterDashboard()

namespace dbuf {

    static lgfx::Bus_RGB* _bus = nullptr;
    static lgfx::Panel_FrameBufferBase* _panel = nullptr;
    static bool _active = false;
    static void (*_syncCB)() = nullptr;

    inline void enable(LGFX_Device& tft, void (*syncCB)() = nullptr) {
        auto bus = (lgfx::Bus_RGB*)tft.getPanel()->getBus();
        if (!bus || !bus->getBackBuffer()) {
            Serial.println("[dbuf] No back buffer — single-buffer mode");
            return;
        }
        _bus = bus;
        _panel = (lgfx::Panel_FrameBufferBase*)tft.getPanel();
        _syncCB = syncCB;

        uint8_t* disp = bus->getDisplayBuffer();
        uint8_t* back = bus->getBackBuffer();
        size_t fb_size = 480 * 480 * 2;
        memcpy(back, disp, fb_size);

        _panel->rebindFrameBuffer(back);

        _active = true;
        Serial.println("[dbuf] Double buffering enabled");
    }

    inline void swapAndWait() {
        if (!_active) return;

        _bus->requestSwap();

        unsigned long t0 = millis();
        while (_bus->swapPending()) {
            if (millis() - t0 > 35) {
                _bus->cancelSwap();
                Serial.println("[dbuf] WARN: swap timeout — frame dropped");
                return;
            }
        }

        _panel->rebindFrameBuffer(_bus->getBackBuffer());

        if (_syncCB) _syncCB();
    }

    inline bool active() { return _active; }

}

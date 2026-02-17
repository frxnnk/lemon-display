#pragma once
#include <Arduino.h>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>

// ── VSync counter (incremented in Bus_RGB ISR) ──
extern volatile uint32_t lgfx_vsync_count;

// ── Wait for next VSync (spin-wait, max ~20ms) ──
inline void waitVSync() {
    uint32_t v0 = lgfx_vsync_count;
    unsigned long t0 = millis();
    while (lgfx_vsync_count == v0) {
        if (millis() - t0 > 20) break;
    }
}

// ── Double-buffer helpers ──
// These work with the Bus_RGB double buffering patch.
// If the second buffer wasn't allocated, all functions gracefully no-op
// and the system behaves as single-buffered (legacy).

namespace dbuf {

    // Cache the bus pointer for fast access (set once in enable())
    static lgfx::Bus_RGB* _bus = nullptr;
    static lgfx::Panel_FrameBufferBase* _panel = nullptr;
    static bool _active = false;
    static void (*_syncCB)() = nullptr;

    /// Call once after dashboard is fully drawn to the display buffer.
    /// syncCB: called after each swap to sync the new draw buffer
    /// (e.g., push all zone sprites so partial updates work correctly).
    inline void enable(LGFX_Device& tft, void (*syncCB)() = nullptr) {
        auto bus = (lgfx::Bus_RGB*)tft.getPanel()->getBus();
        if (!bus || !bus->getBackBuffer()) {
            Serial.println("[dbuf] No back buffer — single-buffer mode");
            return;
        }
        _bus = bus;
        _panel = (lgfx::Panel_FrameBufferBase*)tft.getPanel();
        _syncCB = syncCB;

        // Copy current display buffer → back buffer so both are identical
        uint8_t* disp = bus->getDisplayBuffer();
        uint8_t* back = bus->getBackBuffer();
        size_t fb_size = 480 * 480 * 2;  // RGB565
        memcpy(back, disp, fb_size);

        // Rebind LovyanGFX drawing to back buffer (draw buffer)
        _panel->rebindFrameBuffer(back);

        _active = true;
        Serial.println("[dbuf] Double buffering enabled");
    }

    /// Request buffer swap and block until VSync ISR completes it.
    /// After return, _lines_buffer is rebound to the new draw buffer
    /// and the sync callback is invoked to prepare it for next frame.
    /// If the swap doesn't complete within one frame (~35ms), we cancel
    /// it and drop the frame to avoid drawing on the display buffer.
    inline void swapAndWait() {
        if (!_active) return;

        uint32_t countBefore = lgfx_vsync_count;
        _bus->requestSwap();

        // Spin-wait for ISR to complete the swap (max ~35ms = 1 full frame)
        unsigned long t0 = millis();
        while (_bus->swapPending()) {
            if (millis() - t0 > 35) {
                // Timeout: cancel the pending swap and drop this frame.
                // Without this, the ISR could swap later and leave
                // _lines_buffer pointing to the front buffer → tearing.
                _bus->cancelSwap();
                Serial.println("[dbuf] WARN: swap timeout — frame dropped");
                return;  // Don't rebind, don't sync — keep drawing to same buffer
            }
        }

        // Rebind drawing to the new back buffer
        _panel->rebindFrameBuffer(_bus->getBackBuffer());

        // Sync: push current zone content to new draw buffer.
        // Reads from sprite memory (NOT the display buffer),
        // avoiding PSRAM bus contention with DMA reads.
        if (_syncCB) _syncCB();
    }

    /// Check if double buffering is active
    inline bool active() { return _active; }

}  // namespace dbuf

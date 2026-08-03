#include "display_manager.h"
#include "config.h"
#include "colors.h"

#include <soc/lcd_cam_struct.h>
#include <soc/lcd_cam_reg.h>
#include <soc/lcd_periph.h>
#include <esp_intr_alloc.h>

LGFX tft;

// ── VSync counter (incremented by shared ISR on LCD VSync interrupt) ──
static volatile uint32_t _vsync_count = 0;
static intr_handle_t _vsync_intr = nullptr;
static portMUX_TYPE _display_diag_mux = portMUX_INITIALIZER_UNLOCKED;
static DisplayDiagnostics _display_diag = {};

static void IRAM_ATTR vsync_counter_isr(void*) {
    _vsync_count++;
}

void displaySetup() {
    tft.init();
    tft.setRotation(3);  // 90° left (counter-clockwise)
    tft.setBrightness(255);
    tft.fillScreen(Colors::BG_BASE);

    Serial.printf("[Display] LovyanGFX initialized %dx%d\n", SCREEN_W, SCREEN_H);
}

void displaySetupVSync() {
    // Register a shared ISR on the same LCD interrupt used by Bus_RGB.
    // Both ISRs fire on every VSync — ours just increments a counter.
    //
    // No ESP_INTR_FLAG_IRAM here, and that is not an oversight: LovyanGFX
    // allocates this same vector with ESP_INTR_FLAG_INTRDISABLED |
    // ESP_INTR_FLAG_SHARED and no IRAM (Bus_RGB.cpp, esp32s3). ESP-IDF requires
    // every handler sharing a vector to agree on the IRAM flag, so asking for
    // IRAM found no usable slot and failed with ESP_ERR_NOT_FOUND (261) —
    // silently, leaving _vsync_count frozen and every displayWaitVSync() call
    // burning its full 25 ms timeout.
    int flags = ESP_INTR_FLAG_SHARED;
    esp_err_t err = esp_intr_alloc_intrstatus(
        lcd_periph_signals.panels[0].irq_id,
        flags,
        (uint32_t)&LCD_CAM.lc_dma_int_st,
        1,  // LCD_LL_EVENT_VSYNC_END
        vsync_counter_isr,
        nullptr,
        &_vsync_intr
    );
    if (err == ESP_OK) {
        esp_intr_enable(_vsync_intr);
        // Measure the panel instead of deriving it: the whole frame budget of
        // this project hangs off this number, and the datasheet's "FPS > 50"
        // assumes a pclk we do not use.
        const uint32_t c0 = _vsync_count;
        const uint32_t t0 = millis();
        while (millis() - t0 < 500) vTaskDelay(1);
        const uint32_t ticks = _vsync_count - c0;
        Serial.printf("[Display] VSync ISR registered; panel at %.1f Hz "
                      "(%lu ticks / 500 ms)\n",
                      (float)ticks * 2.0f, (unsigned long)ticks);
    } else {
        Serial.printf("[Display] VSync ISR failed: %d\n", err);
    }
}

void displayWaitVSync() {
    uint32_t v0 = _vsync_count;
    unsigned long t0 = millis();
    bool timedOut = false;
    while (_vsync_count == v0) {
        if (millis() - t0 > 25) {
            timedOut = true;
            break;
        }
        vTaskDelay(1);  // Yield 1ms to RTOS instead of busy-spinning
    }
    portENTER_CRITICAL(&_display_diag_mux);
    _display_diag.waitCalls++;
    if (timedOut) _display_diag.waitTimeouts++;
    portEXIT_CRITICAL(&_display_diag_mux);
}

void displayRecordPush(uint32_t bytes, uint32_t durationUs) {
    portENTER_CRITICAL(&_display_diag_mux);
    _display_diag.pushCount++;
    _display_diag.pushedBytes += bytes;
    _display_diag.lastPushUs = durationUs;
    _display_diag.lastPushBytes = bytes;
    if (durationUs > _display_diag.maxPushUs) _display_diag.maxPushUs = durationUs;
    if (bytes > _display_diag.maxPushBytes) _display_diag.maxPushBytes = bytes;
    portEXIT_CRITICAL(&_display_diag_mux);
}

DisplayDiagnostics displayGetDiagnostics() {
    DisplayDiagnostics snapshot;
    portENTER_CRITICAL(&_display_diag_mux);
    snapshot = _display_diag;
    portEXIT_CRITICAL(&_display_diag_mux);
    snapshot.vsyncCount = _vsync_count;
    return snapshot;
}

void displaySetBrightness(uint8_t level) {
    tft.setBrightness(level);
}

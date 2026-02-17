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
    // Bus_RGB registers its ISR with ESP_INTR_FLAG_SHARED, so we can add ours.
    // Both ISRs fire on every VSync — ours just increments a counter.
    int flags = ESP_INTR_FLAG_SHARED | ESP_INTR_FLAG_IRAM;
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
        Serial.println("[Display] VSync ISR registered");
    } else {
        Serial.printf("[Display] VSync ISR failed: %d\n", err);
    }
}

void displayWaitVSync() {
    uint32_t v0 = _vsync_count;
    unsigned long t0 = millis();
    while (_vsync_count == v0) {
        if (millis() - t0 > 25) break;  // Timeout: ~1.2 frames at 49Hz
    }
}

void displaySetBrightness(uint8_t level) {
    tft.setBrightness(level);
}

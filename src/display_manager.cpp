#include "display_manager.h"
#include "config.h"
#include "colors.h"

LGFX tft;

void displaySetup() {
    tft.init();
    tft.setRotation(3);  // 90° left (counter-clockwise)
    tft.setBrightness(255);
    tft.fillScreen(Colors::BG_BASE);

    Serial.printf("[Display] LovyanGFX initialized %dx%d\n", SCREEN_W, SCREEN_H);
}

void displaySetBrightness(uint8_t level) {
    tft.setBrightness(level);
}

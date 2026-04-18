#include "ui_stocks.h"
#include "ui_views.h"
#include "display_manager.h"
#include "colors.h"

void stocksDrawAll() {
    tft.fillScreen(Colors::BG_BASE);
    tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_center);
    tft.setTextSize(2);
    tft.drawString("STOCKS", 240, 220);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    tft.drawString("Coming soon", 240, 260);
    tft.setTextDatum(lgfx::top_left);
    viewsDrawDotsOverlay();
}

void stocksHandleTouch(const TouchEvent& /*evt*/) {
    // No-op in shell; swipe/tap for view switch is handled upstream.
}

void stocksTick() {
    // No animation yet.
}

#include "ui_views.h"
#include "ui_dashboard.h"
#include "ui_stocks.h"
#include "display_manager.h"
#include "colors.h"
#include <Arduino.h>

static ViewId        currentView = VIEW_CRYPTO;
static ViewRedrawCB  redrawCBs[VIEW_COUNT] = { nullptr };

void viewsInit() {
    currentView = VIEW_CRYPTO;
    for (int i = 0; i < VIEW_COUNT; i++) redrawCBs[i] = nullptr;
    dashboardSetMuted(false);
}

ViewId viewsGetCurrent() {
    return currentView;
}

uint8_t viewsCount() {
    return (uint8_t)VIEW_COUNT;
}

void viewsSetCurrent(ViewId v) {
    if (v >= VIEW_COUNT) return;
    if (v == currentView) return;
    currentView = v;
    // Silence background dashboard draws (WS ticks, morph anim, countdown,
    // etc.) while a non-Crypto view owns the screen — otherwise zone
    // pushSprites overwrite it.
    dashboardSetMuted(v != VIEW_CRYPTO);
    Serial.printf("[Views] switched to %d (dashMuted=%d)\n", (int)v, (int)(v != VIEW_CRYPTO));
    viewsDraw();
}

void viewsCycleNext() {
    uint8_t n = (uint8_t)(currentView + 1);
    if (n >= VIEW_COUNT) n = 0;
    viewsSetCurrent((ViewId)n);
}

void viewsRegisterRedraw(ViewId v, ViewRedrawCB cb) {
    if (v >= VIEW_COUNT) return;
    redrawCBs[v] = cb;
}

void viewsHandleTouch(const TouchEvent& evt) {
    // Intercept the provisional view-switch gesture at the top level so it
    // works across every view (including Stocks, whose own touch handler
    // doesn't know about double-tap). Header zone: y < 44.
    if (evt.gesture == TOUCH_DOUBLE_TAP && evt.y < 44) {
        viewsCycleNext();
        return;
    }

    switch (currentView) {
        case VIEW_CRYPTO:
            dashboardHandleTouch(evt);
            break;
        case VIEW_STOCKS:
            stocksHandleTouch(evt);
            break;
        default:
            break;
    }
}

void viewsTick() {
    switch (currentView) {
        case VIEW_CRYPTO:
            dashboardUpdateFlash();
            break;
        case VIEW_STOCKS:
            stocksTick();
            break;
        default:
            break;
    }
}

void viewsDraw() {
    if (redrawCBs[currentView]) {
        redrawCBs[currentView]();
    }
}

void viewsFillGaps() {
    switch (currentView) {
        case VIEW_CRYPTO:
            dashboardFillGaps();
            break;
        default:
            break;
    }
}

// ── Dots indicator (bottom-center, minimal 4px footprint) ──
void viewsDrawDotsOverlay() {
    if (VIEW_COUNT <= 1) return;
    const int dotR        = 2;
    const int dotSpacing  = 10;
    const int y           = 476;
    const int totalW      = (int)VIEW_COUNT * dotSpacing - (dotSpacing - 2 * dotR);
    const int x0          = (480 - totalW) / 2 + dotR;
    for (int i = 0; i < (int)VIEW_COUNT; i++) {
        uint16_t c = (i == (int)currentView) ? Colors::LEMON_GREEN : Colors::MOON;
        tft.fillCircle(x0 + i * dotSpacing, y, dotR, c);
    }
}

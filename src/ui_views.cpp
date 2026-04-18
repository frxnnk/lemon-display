#include "ui_views.h"
#include "ui_dashboard.h"
#include <Arduino.h>

static ViewId        currentView = VIEW_CRYPTO;
static ViewRedrawCB  redrawCBs[VIEW_COUNT] = { nullptr };

void viewsInit() {
    currentView = VIEW_CRYPTO;
    for (int i = 0; i < VIEW_COUNT; i++) redrawCBs[i] = nullptr;
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
    Serial.printf("[Views] switched to %d\n", (int)v);
    viewsDraw();
}

void viewsRegisterRedraw(ViewId v, ViewRedrawCB cb) {
    if (v >= VIEW_COUNT) return;
    redrawCBs[v] = cb;
}

void viewsHandleTouch(const TouchEvent& evt) {
    switch (currentView) {
        case VIEW_CRYPTO:
            dashboardHandleTouch(evt);
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

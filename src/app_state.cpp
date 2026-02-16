#include "app_state.h"
#include "touch_utils.h"
#include "ui_dashboard.h"
#include "ui_settings.h"
#include "audio_manager.h"
#include "nvs_storage.h"
#include <Arduino.h>

static AppScreen currentScreen  = SCREEN_BOOT_SPLASH;
static AppScreen previousScreen = SCREEN_BOOT_SPLASH;
static DashboardRedrawCB dashRedrawCB = nullptr;

void appSetDashboardRedrawCB(DashboardRedrawCB cb) {
    dashRedrawCB = cb;
}

void appInit() {
    currentScreen  = SCREEN_BOOT_SPLASH;
    previousScreen = SCREEN_BOOT_SPLASH;
}

void appSetScreen(AppScreen screen) {
    previousScreen = currentScreen;
    currentScreen  = screen;
    Serial.printf("[App] Screen: %d -> %d\n", previousScreen, currentScreen);
    appDrawCurrent();
}

AppScreen appGetScreen() {
    return currentScreen;
}

AppScreen appGetPreviousScreen() {
    return previousScreen;
}

// ── Dispatch touch to active screen ──
void appHandleTouch(const TouchEvent& evt) {
    if (evt.gesture == TOUCH_NONE) return;

    // Tap sound
    if (evt.gesture == TOUCH_TAP && nvsGetSoundEnabled()) {
        playTap();
    }

    switch (currentScreen) {
        case SCREEN_DASHBOARD:
            dashboardHandleTouch(evt);
            break;
        case SCREEN_SETTINGS:
            settingsHandleTouch(evt);
            break;
        default:
            break;
    }
}

// ── Per-frame tick ──
void appTick() {
    switch (currentScreen) {
        case SCREEN_DASHBOARD:
            dashboardUpdateFlash();
            break;
        case SCREEN_SETTINGS:
            settingsTick();
            break;
        default:
            break;
    }
}

// ── Draw current screen ──
void appDrawCurrent() {
    switch (currentScreen) {
        case SCREEN_SETTINGS:
            settingsDraw();
            break;
        case SCREEN_DASHBOARD:
            if (dashRedrawCB) dashRedrawCB();
            break;
        // SCREEN_BOOT_SPLASH, SCREEN_WIFI_QR, SCREEN_WIFI_CONNECTING,
        // SCREEN_LOADING are drawn directly by main.cpp
        default:
            break;
    }
}

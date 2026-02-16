#pragma once

#include <cstdint>
#include "touch_manager.h"

// ── Screen identifiers (v2: simplified to 6) ──
enum AppScreen : uint8_t {
    SCREEN_BOOT_SPLASH,       // Logo + brand (1.5s)
    SCREEN_WIFI_QR,           // QR code + captive portal
    SCREEN_WIFI_CONNECTING,   // Brief spinner
    SCREEN_WIFI_FAILED,       // Connection failed — retry or reconfigure
    SCREEN_LOADING,           // Progress bar (fetch data)
    SCREEN_DASHBOARD,         // Main dashboard (5 zones)
    SCREEN_SETTINGS,          // Single-page settings
};

// ── State machine API ──
void       appInit();
void       appSetScreen(AppScreen screen);
AppScreen  appGetScreen();
AppScreen  appGetPreviousScreen();

// Called every frame from loop()
void       appHandleTouch(const TouchEvent& evt);
void       appTick();
void       appDrawCurrent();

// Callback for dashboard redraw (set by main.cpp since it owns the data)
typedef void (*DashboardRedrawCB)();
void       appSetDashboardRedrawCB(DashboardRedrawCB cb);

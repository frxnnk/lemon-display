#pragma once

#include "data_models.h"
#include "touch_manager.h"

void dashboardSetup();

// Touch handling (zones are internal to dashboard)
typedef void (*DashboardTouchCB)(const TouchEvent& evt, uint8_t zoneId);
void dashboardSetTouchCallback(DashboardTouchCB cb);
void dashboardHandleTouch(const TouchEvent& evt);

// ── Individual zone updates (only redraws what changed) ──
void dashboardDrawHeader(const char* timeStr, bool offline = false, bool liveMode = false);
void dashboardDrawBtcHero(const BtcPrice& btc, const SparklineData& spark, uint8_t selectedPeriod);
void dashboardDrawLemonDollar(const LemonPrice& lemon);

// Full redraw
void dashboardDrawAll(const char* timeStr,
                      const BtcPrice& btc, const SparklineData& spark,
                      uint8_t selectedPeriod,
                      const LemonPrice& lemon,
                      bool offline = false, bool liveMode = false);

// Loading screen with progress phases
enum LoadPhase : uint8_t {
    LOAD_LOGO = 0,
    LOAD_WIFI,
    LOAD_NTP,
    LOAD_DATA,
    LOAD_DOLLAR,
    LOAD_CHART,
    LOAD_DONE
};
void dashboardDrawLoading(LoadPhase phase = LOAD_LOGO);

// Offline indicator
void dashboardDrawOffline();

// Non-blocking touch flash feedback
void dashboardStartFlash(uint8_t zoneId);
void dashboardUpdateFlash();

// ── Sub-zone hit tests ──
// Returns 255 if no badge was tapped
uint8_t dashboardHitTestPeriodBadge(int16_t x, int16_t y);

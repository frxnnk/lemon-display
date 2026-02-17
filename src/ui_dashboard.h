#pragma once

#include <cstdint>
#include <cmath>
#include "data_models.h"
#include "touch_manager.h"

void dashboardSetup();

// Touch handling (zones are internal to dashboard)
typedef void (*DashboardTouchCB)(const TouchEvent& evt, uint8_t zoneId);
void dashboardSetTouchCallback(DashboardTouchCB cb);
void dashboardHandleTouch(const TouchEvent& evt);

// ── Individual zone updates (only redraws what changed) ──
void dashboardDrawHeader(const char* timeStr, bool offline = false, bool wsConnected = false);
void dashboardDrawBtcHero(const BtcPrice& btc, const SparklineData& spark, uint8_t selectedPeriod,
                          const float* periodChanges = nullptr,
                          ChartStyle chartStyle = CHART_LINE, const OhlcData* ohlc = nullptr);
void dashboardDrawLemonDollar(const LemonPrice& lemon, const SparklineData* lemonSpark = nullptr,
                              uint8_t dollarPeriod = 1, ChartStyle dollarChartStyle = CHART_LINE,
                              float dollarChange = NAN);
void dashboardDrawPriceOnly(const BtcPrice& btc);  // Partial update — price strip only

// Full redraw
void dashboardDrawAll(const char* timeStr,
                      const BtcPrice& btc, const SparklineData& spark,
                      uint8_t selectedPeriod,
                      const LemonPrice& lemon,
                      bool offline = false, bool wsConnected = false,
                      const float* periodChanges = nullptr,
                      ChartStyle chartStyle = CHART_LINE, const OhlcData* ohlc = nullptr,
                      const SparklineData* lemonSpark = nullptr,
                      uint8_t dollarPeriod = 1,
                      ChartStyle dollarChartStyle = CHART_LINE,
                      float dollarChange = NAN);

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

// Price color flash (green=up, red=down, fades to white over 600ms)
void dashboardFlashPrice(bool up);

// Double-buffer sync: push dirty zone sprites to current draw buffer.
// Called automatically after each buffer swap via callback.
void dashboardSyncDrawBuffer();

// Dirty zone management for selective sync
void dashboardMarkDirty(uint8_t zoneId);   // Mark a single zone as modified
void dashboardMarkAllDirty();              // Mark all zones (for full redraw)

// ── Sub-zone hit tests ──
// Returns -1 (prev), 0 (no hit), +1 (next) for carousel interaction
int8_t dashboardHitTestCarousel(int16_t x, int16_t y);

// Returns -1 (prev), 0 (no hit), +1 (next) for dollar carousel
int8_t dashboardHitTestDollarCarousel(int16_t x, int16_t y);

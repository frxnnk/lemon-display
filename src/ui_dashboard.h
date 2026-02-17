#pragma once

#include <cstdint>
#include <cmath>
#include "data_models.h"
#include "touch_manager.h"

// ── Carousel momentum state ──
struct CarouselState {
    float scrollOffset;   // Fractional position (0.0 = first item visible at top)
    float velocity;       // items/second
    bool  animating;      // true while momentum is active
    int   snappedIndex;   // Last snapped-to index (to avoid redundant fetches)
};

void dashboardSetup();

// Layout presets (0=standard, 1=btc_focus, 2=compact)
void dashboardSetLayout(uint8_t idx);
uint8_t dashboardGetLayout();

// Touch handling (zones are internal to dashboard)
typedef void (*DashboardTouchCB)(const TouchEvent& evt, uint8_t zoneId);
void dashboardSetTouchCallback(DashboardTouchCB cb);
void dashboardHandleTouch(const TouchEvent& evt);

// ── Individual zone updates (only redraws what changed) ──
void dashboardDrawHeader(const char* timeStr, bool offline = false, bool wsConnected = false);
void dashboardDrawBtcHero(const BtcPrice& btc, const SparklineData& spark, uint8_t selectedPeriod,
                          const float* periodChanges = nullptr,
                          ChartStyle chartStyle = CHART_LINE, const OhlcData* ohlc = nullptr,
                          uint8_t selectedPair = 0);
void dashboardDrawLemonDollar(const LemonPrice& lemon, const SparklineData* lemonSpark = nullptr,
                              uint8_t dollarPeriod = 1, ChartStyle dollarChartStyle = CHART_LINE,
                              float dollarChange = NAN);
void dashboardDrawPriceOnly(const BtcPrice& btc, uint8_t selectedPair = 0);  // Partial update — price strip only

// Direct-to-framebuffer updates (no pushSprite, no PSRAM bus contention)
void dashboardUpdateTimeDirect(const char* timeStr);
void dashboardUpdatePriceDirect(const BtcPrice& btc, uint8_t selectedPair = 0);

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
void dashboardSyncDrawBuffer();

// Dirty zone management for selective sync
void dashboardMarkDirty(uint8_t zoneId);
void dashboardMarkAllDirty();
void dashboardFillGaps();

// ── Sub-zone hit tests ──
int8_t dashboardHitTestCarousel(int16_t x, int16_t y);
int8_t dashboardHitTestDollarCarousel(int16_t x, int16_t y);

// ── Pair dropdown ──
bool dashboardIsPairDropdownOpen();
void dashboardOpenPairDropdown(uint8_t currentPair);
void dashboardClosePairDropdown();
int8_t dashboardHitTestPairDropdown(int16_t x, int16_t y);  // returns pair idx 0-4 or -1
bool dashboardHitTestPairLabel(int16_t x, int16_t y);

// ── Carousel momentum ──
extern CarouselState btcCarousel;
extern CarouselState dollarCarouselState;
void updateCarouselPhysics(CarouselState& cs, int maxItems, float dt);

// ── Chart zoom state ──
struct ChartZoomState {
    float zoomLevel;   // 1.0 = no zoom, up to 8.0
    float panOffset;   // 0.0 = leftmost, 1.0 = rightmost (normalized)
    bool  active;      // true when zoom > 1.0
};
extern ChartZoomState chartZoom;

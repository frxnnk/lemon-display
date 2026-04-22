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

// When true, all public dashboardDraw* entry points early-return without
// touching the TFT. Used by the views carousel so other views (Stocks,
// Polymarket) aren't overwritten by WS / scheduler / morph draws that
// keep firing in the background.
void dashboardSetMuted(bool muted);
bool dashboardIsMuted();

// Layout presets (0=standard, 1=btc_focus, 2=compact)
void dashboardSetLayout(uint8_t idx);
uint8_t dashboardGetLayout();

// Z2 slot mode — what lives below the BTC hero.
enum Z2Mode : uint8_t {
    Z2_USD     = 0,   // Lemon Dollar
    Z2_MARKETS = 1,   // Polymarket prediction
    Z2_STOCKS  = 2,   // Watchlist ticker card
    Z2_COUNT   = 3
};
Z2Mode dashboardGetZ2Mode();
void   dashboardSetZ2Mode(Z2Mode mode);
void   dashboardCycleZ2Mode(int8_t dir);   // dir = +1 (next) or -1 (prev)

// Zone geometry accessors (for overlays)
int dashboardGetZ1H();
int dashboardGetZ2Y();
int dashboardGetZ2H();

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

// Compact stocks card rendered into Z2 (pulls data from ui_stocks accessors).
void dashboardDrawStocksZ2();
void dashboardDrawPriceOnly(const BtcPrice& btc, uint8_t selectedPair = 0);  // Partial update — price strip only
void dashboardRedrawChartOnly(const SparklineData& spark, ChartStyle chartStyle, const OhlcData* ohlc = nullptr, float ath = NAN);  // Partial update — chart area only

// Direct-to-framebuffer updates (no pushSprite, no PSRAM bus contention)
void dashboardUpdateTimeDirect(const char* timeStr);
void dashboardUpdatePriceDirect(const BtcPrice& btc, uint8_t selectedPair = 0);

// Full redraw
void dashboardDrawAll(const char* timeStr,
                      const BtcPrice& btc, const SparklineData& spark,
                      uint8_t selectedPeriod,
                      uint8_t selectedPair,
                      const LemonPrice& lemon,
                      bool offline = false, bool wsConnected = false,
                      const float* periodChanges = nullptr,
                      ChartStyle chartStyle = CHART_LINE, const OhlcData* ohlc = nullptr,
                      const SparklineData* lemonSpark = nullptr,
                      uint8_t dollarPeriod = 1,
                      ChartStyle dollarChartStyle = CHART_LINE,
                      float dollarChange = NAN);

// Batch control for atomic transitions (no intermediate vsync/push)
void dashboardBeginBatch();
void dashboardCommitBatch();

// Deferred push: direct-update functions track dirty clips, single vsync on flush
void dashboardSetDeferred(bool defer);
void dashboardFlushDeferred();

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

// Clipped push: render only the pixels inside a spotlight rect (for tutorial overlay)
void dashboardPushSpotlight(int16_t sx, int16_t sy, int16_t sw, int16_t sh);

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
void dashboardSetPairSelectorEnabled(bool enabled);
void dashboardSetBtcCarouselFilter(const uint8_t* idxList, uint8_t count, uint8_t selectedRealIdx);
void dashboardSetDollarCarouselFilter(const uint8_t* idxList, uint8_t count, uint8_t selectedRealIdx);

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

// ── Prediction mode (Polymarket) — renders in Z2 ──
struct PolyMarket;
struct PolyPrediction;
struct PolyStats;
void dashboardSetPredictionLayout(bool active);
struct PredHistoryEntry;
void dashboardDrawPrediction(const PolyMarket* markets, uint8_t count, uint8_t selected,
                              const PolyPrediction* activePred, const PolyStats& stats,
                              bool loading, const char* statusMsg = nullptr,
                              float refPriceUsd = NAN, uint32_t periodStepSec = 300,
                              uint8_t activePeriodIdx = 0,
                              const PredHistoryEntry* history = nullptr,
                              uint8_t histHead = 0, uint8_t histCount = 0,
                              const char* debugMsg = nullptr);
bool dashboardIsPredictionMode();
void dashboardSetPredictionMode(bool active);
bool dashboardHitTestPredYes(int16_t x, int16_t y);
bool dashboardHitTestPredNo(int16_t x, int16_t y);
bool dashboardHitTestPredStats(int16_t x, int16_t y);

// Prediction countdown epoch cache
uint32_t dashboardGetPredEndEpoch();

// Direct countdown update (partial push — no flicker)
void dashboardUpdateCountdownDirect();

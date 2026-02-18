#include <Arduino.h>
#include "config.h"
#include "colors.h"
#include "nvs_storage.h"
#include "app_state.h"
#include "wifi_manager.h"
#include "wifi_provision.h"
#include "time_manager.h"
#include "api_client.h"
#include "display_manager.h"
#include "data/satoshi_fonts.h"
#include "data/lemon_logo.h"
#include "ui_dashboard.h"
#include "ui_components.h"
#include "touch_manager.h"
#include "touch_utils.h"
#include "audio_manager.h"
#include "scheduler.h"
#include "animation.h"
#include "ws_binance.h"
#include <cmath>

// ── Dashboard state ──
struct DashboardState {
    BtcPrice      btc        = {};
    SparklineData spark       = {};
    LemonPrice    lemon       = {};
    OhlcData      ohlc       = {};
    SparklineData lemonSpark  = {};  // Rolling buffer of dollar avg prices
    bool online     = false;
    bool wasOffline = false;
};
static DashboardState state;

// ── Selection state ──
static uint8_t selectedPeriod = 4;  // Default: 24h (index into BTC_PERIODS[])
static uint8_t selectedPair = 0;    // Default: USD (index into BTC_PAIRS[])
static float periodChanges[BTC_PERIOD_COUNT];  // % change per period
static ChartStyle chartStyle = CHART_LINE;
static ChartStyle dollarChartStyle = CHART_LINE;
static float dollarChangePercent = NAN;  // Pre-computed from real sparkline
static uint8_t dollarPeriod = 2;  // Default: 1w (index into DOLLAR_PERIODS[])
static float crossRate = 1.0f;     // For ARS: USDT/ARS rate. For XAU: BTC/XAU direct.

// ── Animators ──
static ValueAnimator btcPriceAnim;
static ValueAnimator lemonBidAnim;
static ValueAnimator lemonAskAnim;
static SparklineAnimator sparkAnim;

// ── Scheduler & task IDs ──
static Scheduler scheduler;
static uint8_t taskClock, taskBtc, taskSparkline, taskLemon, taskDollarSpark, taskCrossRate;

// ── WS price dedup (only redraw when displayed integer changes) ──
static float lastRenderedPrice = 0.0f;
static int   lastDisplayedInt  = 0;
static bool  priceChangedSinceLastDraw = false;

// ── Single-swap-per-frame flag ──
static bool frameDirty = false;

// ── WS visual dirty (rate-limited hero redraw from WS data) ──
static bool wsVisualDirty = false;
static unsigned long lastWsVisualDrawMs = 0;

// ── Dirty-flag rendering coordinator ──
static bool z1Dirty = false;
static bool z2Dirty = false;
static bool z1DrawnThisFrame = false;
static bool z2DrawnThisFrame = false;

// ── Sparkline morph animation ──
#define MORPH_POINTS       120
#define MORPH_DURATION_MS  800
static float morphOldNorm[MORPH_POINTS];
static float morphNewNorm[MORPH_POINTS];
static float morphNewMin, morphNewMax;
static uint32_t morphStartMs;
static bool morphActive = false;
static SparklineData morphedResult;

// ── Dollar sparkline morph animation ──
static float dollarMorphOldNorm[MORPH_POINTS];
static float dollarMorphNewNorm[MORPH_POINTS];
static float dollarMorphNewMin, dollarMorphNewMax;
static uint32_t dollarMorphStartMs;
static bool dollarMorphActive = false;
static SparklineData dollarMorphedResult;

// ── Forward declarations ──
static void enterDashboard();
static void tryConnectSavedWifi();
static void startProvisioning();

// ── Carousel item height for velocity conversion ──
#define CAROUSEL_ITEM_H_PX 34


// ── WiFi failed screen layout ──
#define FAIL_RETRY_Y    280
#define FAIL_RECONF_Y   340
#define FAIL_BTN_W      300
#define FAIL_BTN_H       48
#define FAIL_BTN_X      ((SCREEN_W - FAIL_BTN_W) / 2)

// ── Morph helpers ──
static void resampleNormalize(const SparklineData& src, float* dst, int count) {
    float range = src.maxVal - src.minVal;
    if (range < 0.01f) range = 1.0f;
    for (int i = 0; i < count; i++) {
        float srcIdx = (float)i / (count - 1) * (src.count - 1);
        int lo = (int)srcIdx;
        int hi = lo + 1;
        if (hi >= src.count) hi = src.count - 1;
        float frac = srcIdx - lo;
        float val = src.points[lo] + frac * (src.points[hi] - src.points[lo]);
        dst[i] = (val - src.minVal) / range;
    }
}

static SparklineData& getMorphedSparkline() {
    uint32_t elapsed = millis() - morphStartMs;
    float t = (float)elapsed / MORPH_DURATION_MS;
    if (t >= 1.0f) {
        t = 1.0f;
        morphActive = false;
    }
    float inv = 1.0f - t;
    float ease = 1.0f - (inv * inv * inv * inv * inv);  // ease-out quint

    morphedResult.count = MORPH_POINTS;
    morphedResult.minVal = morphNewMin;
    morphedResult.maxVal = morphNewMax;
    float range = morphNewMax - morphNewMin;
    if (range < 0.01f) range = 1.0f;

    for (int i = 0; i < MORPH_POINTS; i++) {
        float norm = morphOldNorm[i] + ease * (morphNewNorm[i] - morphOldNorm[i]);
        morphedResult.points[i] = morphNewMin + norm * range;
    }
    morphedResult.valid = true;
    return morphedResult;
}

static SparklineData& getDollarMorphedSparkline() {
    uint32_t elapsed = millis() - dollarMorphStartMs;
    float t = (float)elapsed / MORPH_DURATION_MS;
    if (t >= 1.0f) {
        t = 1.0f;
        dollarMorphActive = false;
    }
    float inv = 1.0f - t;
    float ease = 1.0f - (inv * inv * inv * inv * inv);  // ease-out quint

    dollarMorphedResult.count = MORPH_POINTS;
    dollarMorphedResult.minVal = dollarMorphNewMin;
    dollarMorphedResult.maxVal = dollarMorphNewMax;
    float range = dollarMorphNewMax - dollarMorphNewMin;
    if (range < 0.01f) range = 1.0f;

    for (int i = 0; i < MORPH_POINTS; i++) {
        float norm = dollarMorphOldNorm[i] + ease * (dollarMorphNewNorm[i] - dollarMorphOldNorm[i]);
        dollarMorphedResult.points[i] = dollarMorphNewMin + norm * range;
    }
    dollarMorphedResult.valid = true;
    return dollarMorphedResult;
}

// ── Helper: redraw hero with current state (uses morph if active) ──
static void redrawHero() {
    if (morphActive && chartStyle != CHART_CANDLE) {
        SparklineData& morphed = getMorphedSparkline();
        dashboardDrawBtcHero(state.btc, morphed, selectedPeriod, periodChanges, chartStyle, &state.ohlc, selectedPair);
    } else {
        dashboardDrawBtcHero(state.btc, state.spark, selectedPeriod, periodChanges, chartStyle, &state.ohlc, selectedPair);
    }
    z1DrawnThisFrame = true;
    z1Dirty = false;
}

// ── Scheduled callbacks ──
static void updateClock() {
    if (timeReady()) {
        // Direct-to-framebuffer updates (no pushSprite, no PSRAM/DMA bounce)
        dashboardUpdateTimeDirect(getTimeStr(nvsGet24hFormat()).c_str());
        if (priceChangedSinceLastDraw && !z1Dirty) {
            priceChangedSinceLastDraw = false;
            dashboardUpdatePriceDirect(state.btc, selectedPair);
        }
        frameDirty = true;
    }
}

static void updateBtc() {
    if (!state.online) return;
    // CoinGecko: fetch % changes only (price comes from WebSocket)
    BtcPrice tmp = {};
    ApiResult res = fetchBtcPrice(tmp);
    if (res == API_OK) {
        state.btc.change1h  = tmp.change1h;
        state.btc.change24h = tmp.change24h;
        state.btc.change7d  = tmp.change7d;
        state.btc.ath       = tmp.ath;
        state.btc.athChangePercent = tmp.athChangePercent;
        // Fill periodChanges from CoinGecko (mapped to BTC_PERIODS indices)
        periodChanges[2] = tmp.change1h;   // 1h
        periodChanges[4] = tmp.change24h;  // 24h
        // Only update price from CoinGecko if WS has no price yet
        if (!wsBinanceHasPrice()) {
            state.btc.usd = tmp.usd;
            state.btc.valid = tmp.valid;
            btcPriceAnim.set(state.btc.usd);
        }
        z1Dirty = true;
        frameDirty = true;
        if (nvsGetAlertEnabled() && fabsf(state.btc.change1h) >= ALERT_BTC_1H_THRESHOLD_PCT) {
            if (state.btc.change1h > 0) playAlertUp(); else playAlertDown();
        }
    }
}

static void updateSparkline() {
    if (!state.online) return;

    // Save current sparkline for morph animation (static to avoid stack overflow)
    static SparklineData oldSpark;
    oldSpark = state.spark;

    bool ok = false;
    const PeriodDef& pd = BTC_PERIODS[selectedPeriod];
    const PairDef& pair = BTC_PAIRS[selectedPair];

    if (pair.source == PAIR_BINANCE_DIRECT || pair.source == PAIR_BINANCE_INVERT) {
        // Binance pairs: use WS buffer or REST klines with parameterized symbol
        if (pd.useWsBuf) {
            wsBinanceGetSparkline(state.spark);
            int trimTo = pd.limit;
            if (state.spark.valid && state.spark.count > trimTo) {
                int offset = state.spark.count - trimTo;
                state.spark.minVal = 1e12f;
                state.spark.maxVal = -1e12f;
                for (int i = 0; i < trimTo; i++) {
                    state.spark.points[i] = state.spark.points[offset + i];
                    if (state.spark.points[i] < state.spark.minVal) state.spark.minVal = state.spark.points[i];
                    if (state.spark.points[i] > state.spark.maxVal) state.spark.maxVal = state.spark.points[i];
                }
                state.spark.count = trimTo;
            }
            ok = state.spark.valid;
        } else {
            ok = (fetchBinanceKlinesSymbol(state.spark, pair.restSymbol,
                                           pd.klineInterval, pd.limit, pair.inverted) == API_OK);
        }

        // Fetch OHLC data if in candle mode and period supports it
        if (ok && chartStyle == CHART_CANDLE && pd.canOhlc && pd.klineInterval && pair.restSymbol) {
            fetchBinanceOhlcSymbol(state.ohlc, pair.restSymbol,
                                   pd.klineInterval, pd.limit, pair.inverted);
        }
    } else if (pair.source == PAIR_DERIVED) {
        // DERIVED (ARS): use btcusdt WS buffer * crossRate for short periods, CoinGecko for REST
        if (pd.useWsBuf) {
            wsBinanceGetSparkline(state.spark);
            int trimTo = pd.limit;
            if (state.spark.valid && state.spark.count > trimTo) {
                int offset = state.spark.count - trimTo;
                state.spark.minVal = 1e12f;
                state.spark.maxVal = -1e12f;
                for (int i = 0; i < trimTo; i++) {
                    state.spark.points[i] = state.spark.points[offset + i];
                    if (state.spark.points[i] < state.spark.minVal) state.spark.minVal = state.spark.points[i];
                    if (state.spark.points[i] > state.spark.maxVal) state.spark.maxVal = state.spark.points[i];
                }
                state.spark.count = trimTo;
            }
            if (state.spark.valid && crossRate > 0) {
                state.spark.minVal = 1e12f;
                state.spark.maxVal = -1e12f;
                for (int i = 0; i < state.spark.count; i++) {
                    state.spark.points[i] *= crossRate;
                    if (state.spark.points[i] < state.spark.minVal) state.spark.minVal = state.spark.points[i];
                    if (state.spark.points[i] > state.spark.maxVal) state.spark.maxVal = state.spark.points[i];
                }
            }
            ok = state.spark.valid;
        } else {
            int days = pd.limit;
            if (pd.klineInterval) {
                if (strcmp(pd.klineInterval, "5m") == 0) days = 1;
                else if (strcmp(pd.klineInterval, "15m") == 0) days = 1;
                else if (strcmp(pd.klineInterval, "8h") == 0) days = 30;
                else if (strcmp(pd.klineInterval, "1d") == 0) {
                    if (pd.limit <= 180) days = 180;
                    else days = 365;
                }
            }
            ok = (fetchSparklineVsCurrency(state.spark, days, pair.geckoVs) == API_OK);
        }
    } else {
        // GECKO_ONLY (XAU): ALWAYS use CoinGecko — WS buffer * crossRate is meaningless
        int days;
        if (pd.useWsBuf) {
            days = 1;  // Short periods → 1 day from CoinGecko
        } else {
            days = pd.limit;
            if (pd.klineInterval) {
                if (strcmp(pd.klineInterval, "5m") == 0) days = 1;
                else if (strcmp(pd.klineInterval, "15m") == 0) days = 1;
                else if (strcmp(pd.klineInterval, "8h") == 0) days = 30;
                else if (strcmp(pd.klineInterval, "1d") == 0) {
                    if (pd.limit <= 180) days = 180;
                    else days = 365;
                }
            }
        }
        ok = (fetchSparklineVsCurrency(state.spark, days, pair.geckoVs) == API_OK);
    }

    if (ok) {
        // Compute % change from sparkline first/last for REST periods without CoinGecko data
        if (!pd.useWsBuf && state.spark.count >= 2) {
            float first = state.spark.points[0];
            float last  = state.spark.points[state.spark.count - 1];
            if (first > 0) {
                periodChanges[selectedPeriod] = ((last - first) / first) * 100.0f;
            }
        }
        // Start morph animation: interpolate from old to new sparkline over 800ms
        if (oldSpark.valid && oldSpark.count >= 2 && chartStyle != CHART_CANDLE) {
            resampleNormalize(oldSpark, morphOldNorm, MORPH_POINTS);
            resampleNormalize(state.spark, morphNewNorm, MORPH_POINTS);
            morphNewMin = state.spark.minVal;
            morphNewMax = state.spark.maxVal;
            morphStartMs = millis();
            morphActive = true;
            // Morph driver in loop() handles redraws at 30fps — skip immediate push
            // to avoid double-push bounce (touch handler already pushed Z1)
        } else {
            // No morph possible — mark dirty for consolidated render
            z1Dirty = true;
        }
        frameDirty = true;
    }
}

static void updateLemon() {
    if (!state.online) return;
    if (fetchLemonPrice(state.lemon) == API_OK) {
        lemonBidAnim.set(state.lemon.bid);
        lemonAskAnim.set(state.lemon.ask);

        // Update crossRate if ARS pair is active
        if (BTC_PAIRS[selectedPair].source == PAIR_DERIVED) {
            crossRate = (state.lemon.bid + state.lemon.ask) / 2.0f;
            priceChangedSinceLastDraw = true;
        }

        // Skip redraw during morph — the morph driver handles Z2 at 30fps
        if (!dollarMorphActive) {
            z2Dirty = true;
        }
        frameDirty = true;
    }
}

static void updateDollarSparkline() {
    if (!state.online) return;

    // Save old sparkline for morph
    static SparklineData oldDollarSpark;
    oldDollarSpark = state.lemonSpark;

    if (fetchLemonSparkline(state.lemonSpark, DOLLAR_PERIODS[dollarPeriod].days) == API_OK) {
        // Pre-compute % change from real sparkline (morph won't corrupt it)
        if (state.lemonSpark.count >= 2) {
            float first = state.lemonSpark.points[0];
            float last = state.lemonSpark.points[state.lemonSpark.count - 1];
            if (first > 0 && last > 0 && first > last * 0.01f) {
                dollarChangePercent = ((last - first) / first) * 100.0f;
            } else {
                dollarChangePercent = NAN;
            }
        }
        // Start dollar morph animation
        if (oldDollarSpark.valid && oldDollarSpark.count >= 2) {
            resampleNormalize(oldDollarSpark, dollarMorphOldNorm, MORPH_POINTS);
            resampleNormalize(state.lemonSpark, dollarMorphNewNorm, MORPH_POINTS);
            dollarMorphNewMin = state.lemonSpark.minVal;
            dollarMorphNewMax = state.lemonSpark.maxVal;
            dollarMorphStartMs = millis();
            dollarMorphActive = true;
            // Morph driver in loop() handles redraws at 30fps — skip immediate push
        } else {
            z2Dirty = true;
        }
        frameDirty = true;
    }
}

// ── Cross-rate update (for DERIVED and GECKO_ONLY pairs) ──
static void updateCrossRate() {
    if (!state.online) return;
    if (selectedPair == 0) return;  // USD pair doesn't need crossRate

    const PairDef& pair = BTC_PAIRS[selectedPair];

    if (pair.source == PAIR_DERIVED) {
        // ARS: crossRate = USDT/ARS avg from Lemon
        if (state.lemon.valid) {
            crossRate = (state.lemon.bid + state.lemon.ask) / 2.0f;
        }
    } else if (pair.source == PAIR_GECKO_ONLY) {
        // XAU: crossRate = BTC/XAU from CoinGecko
        float price = 0;
        if (fetchGeckoBtcPrice(pair.geckoVs, price) == API_OK) {
            crossRate = price;
        }
    }

    // Force price redraw when cross-rate changes
    priceChangedSinceLastDraw = true;
    Serial.printf("[Main] CrossRate for %s: %.4f\n", pair.label, crossRate);
}

// ── Switch active trading pair ──
static void switchPair(uint8_t newPair) {
    if (newPair >= BTC_PAIR_COUNT) return;
    selectedPair = newPair;
    const PairDef& pair = BTC_PAIRS[selectedPair];

    Serial.printf("[Main] Switching to pair: %s (%s)\n", pair.pairLabel, pair.label);

    // Clamp period to pair's minimum
    if (selectedPeriod < pair.minPeriodIdx) {
        selectedPeriod = pair.minPeriodIdx;
    }

    // Reset chart state
    state.ohlc.valid = false;
    state.spark.valid = false;
    morphActive = false;
    chartZoom = { 1.0f, 1.0f, false };

    // Reset period changes
    for (int i = 0; i < BTC_PERIOD_COUNT; i++) periodChanges[i] = NAN;

    // ── Immediate visual update: show new pair label, loading state ──
    // Invalidate price — WS still has old pair data, don't show stale number
    state.btc.valid = false;
    redrawHero();
    frameDirty = true;

    // ── Network: reconnect WS + backfill (blocking but hero is already drawn) ──
    switch (pair.source) {
        case PAIR_BINANCE_DIRECT:
        case PAIR_BINANCE_INVERT:
            wsBinanceReconnect(pair.wsPath, pair.inverted);
            wsBinanceBackfillSymbol(pair.restSymbol, pair.inverted);
            break;
        case PAIR_DERIVED:
            if (BTC_PAIRS[0].source == PAIR_BINANCE_DIRECT) {
                wsBinanceReconnect(BTC_PAIRS[0].wsPath, false);
                wsBinanceBackfillSymbol(BTC_PAIRS[0].restSymbol, false);
            }
            break;
        case PAIR_GECKO_ONLY:
            wsBinanceReconnect(BTC_PAIRS[0].wsPath, false);
            wsBinanceBackfillSymbol(BTC_PAIRS[0].restSymbol, false);
            {
                float price = 0;
                if (fetchGeckoBtcPrice(pair.geckoVs, price) == API_OK) {
                    crossRate = price;
                    state.btc.usd = crossRate;
                    state.btc.valid = true;
                    btcPriceAnim.set(state.btc.usd);
                    lastRenderedPrice = state.btc.usd;
                    lastDisplayedInt = (int)state.btc.usd;
                }
            }
            break;
    }

    // ── Set correct price after network reconnect ──
    if (pair.source == PAIR_DERIVED && state.lemon.valid) {
        crossRate = (state.lemon.bid + state.lemon.ask) / 2.0f;
    }
    if (wsBinanceHasPrice()) {
        float wsPrice = wsBinanceGetPrice();
        switch (pair.source) {
            case PAIR_BINANCE_DIRECT:
            case PAIR_BINANCE_INVERT:
                state.btc.usd = wsPrice;
                break;
            case PAIR_DERIVED:
                state.btc.usd = wsPrice * crossRate;
                break;
            case PAIR_GECKO_ONLY:
                state.btc.usd = crossRate;
                break;
        }
        state.btc.valid = true;
        btcPriceAnim.set(state.btc.usd);
        lastRenderedPrice = state.btc.usd;
        lastDisplayedInt = (int)state.btc.usd;
    }

    // Force sparkline refresh + redraw with new data
    scheduler.forceRun(taskSparkline);
    z1Dirty = true;
}

// ── Dashboard touch callback ──

static void onDashboardTouch(const TouchEvent& evt, uint8_t zoneId) {
    // ── Z1: BTC Hero — dropdown selector, chart style, period carousel ──
    if (zoneId == 1) {
        // ── Dropdown guard: swallow all gestures while open ──
        if (dashboardIsPairDropdownOpen()) {
            if (evt.gesture == TOUCH_TAP) {
                int8_t hit = dashboardHitTestPairDropdown(evt.x, evt.y);
                if (hit >= 0 && hit != (int8_t)selectedPair) {
                    dashboardClosePairDropdown();
                    switchPair((uint8_t)hit);
                } else {
                    dashboardClosePairDropdown();
                    redrawHero();
                }
            }
            frameDirty = true;
            return;
        }

        // Pinch-to-zoom
        if (evt.gesture == TOUCH_PINCH) {
            chartZoom.zoomLevel *= evt.pinchScale;
            if (chartZoom.zoomLevel < 1.0f) chartZoom.zoomLevel = 1.0f;
            if (chartZoom.zoomLevel > 8.0f) chartZoom.zoomLevel = 8.0f;
            chartZoom.active = (chartZoom.zoomLevel > 1.01f);
            Serial.printf("[Touch] Zoom: %.1fx\n", chartZoom.zoomLevel);
            redrawHero();
            frameDirty = true;
            return;
        }

        // Double-tap: reset zoom
        if (evt.gesture == TOUCH_DOUBLE_TAP) {
            chartZoom.zoomLevel = 1.0f;
            chartZoom.panOffset = 1.0f;
            chartZoom.active = false;
            Serial.println("[Touch] Zoom reset");
            redrawHero();
            frameDirty = true;
            return;
        }

        // When zoomed, swipe L/R pans instead of changing period
        if (chartZoom.active && (evt.gesture == TOUCH_SWIPE_LEFT || evt.gesture == TOUCH_SWIPE_RIGHT)) {
            float panStep = 0.15f / chartZoom.zoomLevel;
            if (evt.gesture == TOUCH_SWIPE_LEFT) {
                chartZoom.panOffset += panStep;
            } else {
                chartZoom.panOffset -= panStep;
            }
            if (chartZoom.panOffset < 0.0f) chartZoom.panOffset = 0.0f;
            if (chartZoom.panOffset > 1.0f) chartZoom.panOffset = 1.0f;
            Serial.printf("[Touch] Pan: %.2f\n", chartZoom.panOffset);
            redrawHero();
            frameDirty = true;
            return;
        }

        // Fling: start momentum scroll for period carousel
        if (evt.gesture == TOUCH_FLING_UP || evt.gesture == TOUCH_FLING_DOWN) {
            float velItems = evt.velocityY / (float)(CAROUSEL_ITEM_H_PX + 2);
            btcCarousel.scrollOffset = (float)selectedPeriod;
            btcCarousel.velocity = velItems;
            btcCarousel.animating = true;
            return;
        }

        // ── Check pair label tap (opens dropdown) ──
        if (evt.gesture == TOUCH_TAP) {
            if (dashboardHitTestPairLabel(evt.x, evt.y)) {
                dashboardOpenPairDropdown(selectedPair);
                redrawHero();
                frameDirty = true;
                return;
            }
        }

        // ── Period carousel (right side) ──
        int8_t dir = 0;

        if (evt.gesture == TOUCH_TAP) {
            dir = dashboardHitTestCarousel(evt.x, evt.y);
        } else if (evt.gesture == TOUCH_SWIPE_LEFT) {
            dir = +1;  // Next period
        } else if (evt.gesture == TOUCH_SWIPE_RIGHT) {
            dir = -1;  // Previous period
        } else if (evt.gesture == TOUCH_SWIPE_UP) {
            dir = +1;  // Swipe up = next (higher index)
        } else if (evt.gesture == TOUCH_SWIPE_DOWN) {
            dir = -1;  // Swipe down = prev (lower index)
        }

        if (dir != 0) {
            int8_t newPeriod = (int8_t)selectedPeriod + dir;
            uint8_t minIdx = BTC_PAIRS[selectedPair].minPeriodIdx;
            if (newPeriod < (int8_t)minIdx) newPeriod = (int8_t)minIdx;
            if (newPeriod >= 0 && newPeriod < BTC_PERIOD_COUNT) {
                selectedPeriod = (uint8_t)newPeriod;
                Serial.printf("[Touch] Period: %s\n", BTC_PERIODS[selectedPeriod].label);

                // Invalidate OHLC on period change
                state.ohlc.valid = false;

                // Auto-fallback from candle to line for WS-only periods
                if (chartStyle == CHART_CANDLE && !BTC_PERIODS[selectedPeriod].canOhlc) {
                    chartStyle = CHART_LINE;
                    showToast("OHLC no disponible");
                    // Redraw header to show toast in sprite
                    String t = timeReady() ? getTimeStr(nvsGet24hFormat()) : String("--:--:--");
                    dashboardDrawHeader(t.c_str(), !state.online, wsBinanceConnected());
                }

                redrawHero();
                frameDirty = true;
                scheduler.forceRun(taskSparkline);
            }
            return;
        }

        // Tap outside both carousels: cycle chart style
        if (evt.gesture == TOUCH_TAP) {
            int newStyle = ((int)chartStyle + 1) % CHART_STYLE_COUNT;

            // Skip CHART_CANDLE for periods without OHLC support
            if ((ChartStyle)newStyle == CHART_CANDLE && !BTC_PERIODS[selectedPeriod].canOhlc) {
                newStyle = (newStyle + 1) % CHART_STYLE_COUNT;
            }

            chartStyle = (ChartStyle)newStyle;
            Serial.printf("[Touch] Chart style: %d\n", chartStyle);

            if (chartStyle == CHART_CANDLE && !state.ohlc.valid) {
                scheduler.forceRun(taskSparkline);
            }

            redrawHero();
        }
        // Long press: force refresh
        else if (evt.gesture == TOUCH_LONG_PRESS) {
            Serial.println("[Touch] Force refresh: BTC + Sparkline");
            dashboardStartFlash(1);
            redrawHero();           // Immediate redraw to show flash border
            frameDirty = true;
            scheduler.forceRun(taskBtc);
            scheduler.forceRun(taskSparkline);
        }
        return;
    }

    // ── Z2: Dollar — carousel + swipe/fling for period, tap outside = refresh ──
    if (zoneId == 2) {
        // Fling: start momentum scroll
        if (evt.gesture == TOUCH_FLING_UP || evt.gesture == TOUCH_FLING_DOWN) {
            float velItems = evt.velocityY / (float)(28 + 2);  // Dollar carousel item height
            dollarCarouselState.scrollOffset = (float)dollarPeriod;
            dollarCarouselState.velocity = velItems;
            dollarCarouselState.animating = true;
            return;
        }

        int8_t dir = 0;

        if (evt.gesture == TOUCH_TAP) {
            dir = dashboardHitTestDollarCarousel(evt.x, evt.y);
        } else if (evt.gesture == TOUCH_SWIPE_LEFT) {
            dir = +1;
        } else if (evt.gesture == TOUCH_SWIPE_RIGHT) {
            dir = -1;
        } else if (evt.gesture == TOUCH_SWIPE_UP) {
            dir = +1;
        } else if (evt.gesture == TOUCH_SWIPE_DOWN) {
            dir = -1;
        }

        if (dir != 0) {
            int8_t newP = (int8_t)dollarPeriod + dir;
            if (newP >= 0 && newP < DOLLAR_PERIOD_COUNT) {
                dollarPeriod = (uint8_t)newP;
                Serial.printf("[Touch] Dollar period: %s\n", DOLLAR_PERIODS[dollarPeriod].label);
                dollarChangePercent = NAN;
                dashboardDrawLemonDollar(state.lemon, &state.lemonSpark, dollarPeriod, dollarChartStyle, dollarChangePercent);
                z2DrawnThisFrame = true;
                z2Dirty = false;
                frameDirty = true;
                scheduler.forceRun(taskDollarSpark);
            }
            return;
        }

        // Tap outside carousel: cycle chart style (LINE ↔ MARKERS, skip CANDLE)
        if (evt.gesture == TOUCH_TAP) {
            dollarChartStyle = (dollarChartStyle == CHART_LINE) ? CHART_MARKERS : CHART_LINE;
            Serial.printf("[Touch] Dollar chart style: %d\n", dollarChartStyle);
            dashboardDrawLemonDollar(state.lemon, &state.lemonSpark, dollarPeriod, dollarChartStyle, dollarChangePercent);
            z2DrawnThisFrame = true;
            z2Dirty = false;
            frameDirty = true;
        }
        // Long press: force refresh
        else if (evt.gesture == TOUCH_LONG_PRESS) {
            Serial.println("[Touch] Force refresh: Lemon");
            dashboardStartFlash(2);
            dashboardDrawLemonDollar(state.lemon, &state.lemonSpark, dollarPeriod, dollarChartStyle, dollarChangePercent);  // Immediate flash border
            z2DrawnThisFrame = true;
            z2Dirty = false;
            frameDirty = true;
            scheduler.forceRun(taskLemon);
            scheduler.forceRun(taskDollarSpark);
        }
        return;
    }

    if (evt.gesture == TOUCH_TAP) {
        if (zoneId == 0) {
            updateClock();
        }
    } else if (evt.gesture == TOUCH_LONG_PRESS && zoneId == 0) {
        appSetScreen(SCREEN_SETTINGS);
    }
}

// ── Redraw dashboard ──
static void redrawDashboard() {
    String timeStr = timeReady() ? getTimeStr(nvsGet24hFormat()) : String("--:--:--");
    dashboardDrawAll(timeStr.c_str(),
                     state.btc, state.spark, selectedPeriod,
                     state.lemon, !state.online, wsBinanceConnected(),
                     periodChanges, chartStyle, &state.ohlc, &state.lemonSpark,
                     dollarPeriod, dollarChartStyle, dollarChangePercent);
    frameDirty = true;
}

// ── Draw WiFi Failed screen ──
static void drawWifiFailedScreen() {
    tft.fillScreen(Colors::BG_BASE);

    // Icon / title
    tft.setTextColor(Colors::NEGATIVE, Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString("!", SCREEN_W / 2, 120, &SatoshiBold40);

    tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    tft.drawString("No se pudo conectar", SCREEN_W / 2, 180, &SatoshiMedium18);

    // Show which SSID we tried
    char ssid[33];
    char pass[65];
    nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));

    char buf[64];
    snprintf(buf, sizeof(buf), "Red: %s", ssid);
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    tft.drawString(buf, SCREEN_W / 2, 220, &Satoshi12);

    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    tft.drawString("Verifica la contrasena o el router", SCREEN_W / 2, 248, &Satoshi9);

    // Button: Reintentar (green outline)
    tft.fillSmoothRoundRect(FAIL_BTN_X, FAIL_RETRY_Y, FAIL_BTN_W, FAIL_BTN_H, 12, Colors::BG_SURFACE);
    tft.drawRoundRect(FAIL_BTN_X, FAIL_RETRY_Y, FAIL_BTN_W, FAIL_BTN_H, 12, Colors::LEMON_GREEN);
    tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_SURFACE);
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString("Reintentar", FAIL_BTN_X + FAIL_BTN_W / 2, FAIL_RETRY_Y + FAIL_BTN_H / 2, &Satoshi12);

    // Button: Reconfigurar WiFi (red outline)
    tft.fillSmoothRoundRect(FAIL_BTN_X, FAIL_RECONF_Y, FAIL_BTN_W, FAIL_BTN_H, 12, Colors::BG_SURFACE);
    tft.drawRoundRect(FAIL_BTN_X, FAIL_RECONF_Y, FAIL_BTN_W, FAIL_BTN_H, 12, Colors::NEGATIVE);
    tft.setTextColor(Colors::NEGATIVE, Colors::BG_SURFACE);
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString("Reconfigurar WiFi", FAIL_BTN_X + FAIL_BTN_W / 2, FAIL_RECONF_Y + FAIL_BTN_H / 2, &Satoshi12);
}

// ── Try connecting with saved WiFi credentials ──
static void tryConnectSavedWifi() {
    char ssid[33], pass[65];
    nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));

    wifiSetup(ssid, pass);
    state.online = wifiConnected();

    if (state.online) {
        enterDashboard();
    } else {
        Serial.println("[Main] WiFi failed — showing fail screen");
        drawWifiFailedScreen();
        appSetScreen(SCREEN_WIFI_FAILED);
    }
}

// ── Start QR provisioning flow ──
static void startProvisioning() {
    nvsForgetWifi();
    provisionStart();
    provisionDrawQR();
    appSetScreen(SCREEN_WIFI_QR);
}

// ── Enter dashboard: fetch data + draw ──
static void enterDashboard() {
    appSetScreen(SCREEN_LOADING);

    // Ensure clean loading screen (needed when coming from provisioning/connecting)
    displayWaitVSync();
    tft.fillScreen(Colors::BG_BASE);
    drawLemonImagotipo244(tft, (SCREEN_W - 244) / 2, 170);
    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::top_center);
    tft.drawString("v" APP_VERSION, SCREEN_W / 2, SCREEN_H - 30, &Satoshi9);

    dashboardDrawLoading(LOAD_NTP);
    timeSetup();
    apiSetup();

    unsigned long start = millis();
    while (!timeReady() && millis() - start < 5000) {
        delay(100);
    }

    dashboardDrawLoading(LOAD_DATA);
    Serial.println("[Main] Initial data fetch...");
    fetchBtcPrice(state.btc);

    // Initialize BTC price animator and period changes from CoinGecko
    btcPriceAnim.set(state.btc.usd);
    if (state.btc.valid) {
        periodChanges[2] = state.btc.change1h;   // 1h
        periodChanges[4] = state.btc.change24h;  // 24h
    }

    dashboardDrawLoading(LOAD_DOLLAR);
    Serial.println("[Main] Fetching Lemon dollar...");
    fetchLemonPrice(state.lemon);
    lemonBidAnim.set(state.lemon.bid);
    lemonAskAnim.set(state.lemon.ask);
    // Fetch initial dollar sparkline (default period)
    fetchLemonSparkline(state.lemonSpark, DOLLAR_PERIODS[dollarPeriod].days);
    if (state.lemonSpark.valid && state.lemonSpark.count >= 2) {
        float first = state.lemonSpark.points[0];
        float last = state.lemonSpark.points[state.lemonSpark.count - 1];
        if (first > 0 && last > 0 && first > last * 0.01f) {
            dollarChangePercent = ((last - first) / first) * 100.0f;
        } else {
            dollarChangePercent = NAN;
        }
    }

    dashboardDrawLoading(LOAD_CHART);
    // Backfill sparkline from Binance REST (96 x 1m klines)
    if (wsBinanceBackfill()) {
        wsBinanceGetSparkline(state.spark);
        // Set initial price from backfill if CoinGecko didn't provide one
        if (wsBinanceHasPrice() && state.btc.usd <= 0) {
            state.btc.usd = wsBinanceGetPrice();
            state.btc.valid = true;
            btcPriceAnim.set(state.btc.usd);
        }
    } else {
        // Fallback to CoinGecko sparkline
        fetchSparkline(state.spark, 7);
    }

    // Start WebSocket for real-time price updates
    wsBinanceSetup();
    lastRenderedPrice = state.btc.usd;

    dashboardDrawLoading(LOAD_DONE);
    delay(300);

    String timeStr = getTimeStr(nvsGet24hFormat());
    dashboardDrawAll(timeStr.c_str(),
                     state.btc, state.spark, selectedPeriod,
                     state.lemon, !state.online, wsBinanceConnected(),
                     periodChanges, chartStyle, &state.ohlc, &state.lemonSpark,
                     dollarPeriod, dollarChartStyle, dollarChangePercent);

    // Double buffering disabled — was causing vertical bounce/repeat artifacts.
    // All draws go through sprites (toast, flash in sprite) so tearing is minimal.
    // dbuf::enable(tft, dashboardSyncDrawBuffer);

    appSetScreen(SCREEN_DASHBOARD);
}

void setup() {
#if DISPLAY_DIAG_MODE
    Serial.begin(115200);
    Serial.println("\n=== Lemon Display DIAG mode ===");
    displaySetup();
    tft.fillScreen(Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_center);
    tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    tft.drawString("DISPLAY DIAG", SCREEN_W / 2, (SCREEN_H / 2) - 18, &SatoshiMedium18);
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    tft.drawString("Pantalla minima sin UI", SCREEN_W / 2, (SCREEN_H / 2) + 14, &Satoshi12);
    return;
#endif

    Serial.begin(115200);
    Serial.println("\n=== Lemon Interface v4.0 (MaTouch 480x480) ===");

    // Initialize period changes to NAN
    for (int i = 0; i < BTC_PERIOD_COUNT; i++) periodChanges[i] = NAN;

    nvsInit();

    displaySetup();
    displaySetupVSync();
    displaySetBrightness(nvsGetBrightness());

    dashboardSetup();
    dashboardSetLayout(nvsGetLayout());
    dashboardDrawLoading(LOAD_LOGO);

    audioSetup();
    audioSetEnabled(nvsGetSoundEnabled());

    touchSetup();
    appInit();

    dashboardSetTouchCallback(onDashboardTouch);
    appSetDashboardRedrawCB(redrawDashboard);

    taskClock     = scheduler.add("clock",     UPDATE_CLOCK_MS,      updateClock);
    taskBtc       = scheduler.add("btc",       UPDATE_BTC_PRICE_MS,  updateBtc);
    taskSparkline = scheduler.add("sparkline", UPDATE_SPARKLINE_MS,  updateSparkline);
    taskLemon     = scheduler.add("lemon",     UPDATE_LEMON_MS,      updateLemon);
    taskDollarSpark = scheduler.add("dolarSpark", UPDATE_SPARKLINE_MS, updateDollarSparkline);
    taskCrossRate   = scheduler.add("crossRate", 60000, updateCrossRate);  // 60s for XAU/ARS rates

    // ── Boot flow ──
    if (nvsHasWifi()) {
        dashboardDrawLoading(LOAD_WIFI);
        tryConnectSavedWifi();
    } else {
        Serial.println("[Main] No WiFi configured — starting QR provisioning");
        startProvisioning();
    }

    playStartup();
    Serial.println("[Main] Ready");
}

void loop() {
#if DISPLAY_DIAG_MODE
    delay(20);
    return;
#endif

    z1DrawnThisFrame = false;
    z2DrawnThisFrame = false;

    AppScreen screen = appGetScreen();

    // ── WiFi QR Provisioning mode ──
    if (screen == SCREEN_WIFI_QR) {
        if (provisionTick()) {
            char ssid[33], pass[65];
            provisionGetCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
            nvsSaveWifi(ssid, pass);
            provisionStop();

            // Show connecting screen with logo + glass card
            tft.fillScreen(Colors::BG_BASE);

            // Lemon imagotipo centered
            int cLogoX = (SCREEN_W - 122) / 2;
            drawLemonImagotipo122(tft, cLogoX, 150);

            // Glass card with connection info
            {
                int cW = 320, cH = 130;
                int cX = (SCREEN_W - cW) / 2;
                int cY = 210;
                tft.fillSmoothRoundRect(cX, cY, cW, cH, 16, Colors::CARD_BORDER);
                tft.fillSmoothRoundRect(cX + 1, cY + 1, cW - 2, cH - 2, 15, Colors::BG_CARD);

                // Top highlight
                tft.drawFastHLine(cX + 16, cY + 1, cW - 32, Colors::BG_ELEVATED);

                tft.setTextDatum(lgfx::middle_center);
                tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
                tft.drawString("Conectando...", SCREEN_W / 2, cY + 40, &SatoshiMedium18);

                tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
                tft.drawString(ssid, SCREEN_W / 2, cY + 72, &Satoshi12);

                tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
                tft.drawString("Esto puede tardar unos segundos", SCREEN_W / 2, cY + 102, &Satoshi9);
            }

            appSetScreen(SCREEN_WIFI_CONNECTING);

            wifiSetup(ssid, pass);
            state.online = wifiConnected();

            if (state.online) {
                enterDashboard();
            } else {
                Serial.println("[Main] Post-provision connect failed");
                drawWifiFailedScreen();
                appSetScreen(SCREEN_WIFI_FAILED);
            }
        }
        TouchEvent evt = touchLoop();
        (void)evt;
        delay(50);
        return;
    }

    // ── WiFi Failed screen — handle touch ──
    if (screen == SCREEN_WIFI_FAILED) {
        TouchEvent evt = touchLoop();
        if (evt.gesture == TOUCH_TAP) {
            // "Reintentar" button
            if (touchInRect(evt.x, evt.y, FAIL_BTN_X, FAIL_RETRY_Y, FAIL_BTN_W, FAIL_BTN_H)) {
                Serial.println("[Main] Retry WiFi connection");
                tryConnectSavedWifi();
            }
            // "Reconfigurar WiFi" button
            else if (touchInRect(evt.x, evt.y, FAIL_BTN_X, FAIL_RECONF_Y, FAIL_BTN_W, FAIL_BTN_H)) {
                Serial.println("[Main] Reconfigure WiFi — starting provisioning");
                startProvisioning();
            }
        }
        delay(20);
        return;
    }

    // ── Dashboard mode ──
    if (screen == SCREEN_DASHBOARD) {
        wifiLoop();
        state.online = wifiConnected();

        if (!state.online && !state.wasOffline) {
            wsBinanceStop();
            state.wasOffline = true;
            showToast("WiFi desconectado");
            dashboardDrawHeader("--:--:--", true);  // Will render toast bar in sprZ0
            frameDirty = true;
        } else if (state.online && state.wasOffline) {
            state.wasOffline = false;
            timeSetup();
            wsBinanceSetup();
            showToast("WiFi reconectado");
            dashboardDrawHeader(getTimeStr(nvsGet24hFormat()).c_str(), false, wsBinanceConnected());  // Toast bar
            frameDirty = true;
        }

        // WebSocket loop — must run every iteration
        wsBinanceLoop();

        // Check if WS has a new price — only redraw when displayed integer changes
        if (wsBinanceHasPrice()) {
            float wsPrice = wsBinanceGetPrice();

            // Apply pair-specific price transformation
            float displayPrice = wsPrice;
            const PairDef& curPair = BTC_PAIRS[selectedPair];
            switch (curPair.source) {
                case PAIR_BINANCE_DIRECT:
                case PAIR_BINANCE_INVERT:
                    displayPrice = wsPrice;  // Already direct or inverted by WS module
                    break;
                case PAIR_DERIVED:
                    displayPrice = wsPrice * crossRate;
                    break;
                case PAIR_GECKO_ONLY:
                    displayPrice = crossRate;  // Fetched directly from CoinGecko
                    break;
            }

            if (displayPrice != lastRenderedPrice) {
                int newInt = (int)displayPrice;
                if (lastRenderedPrice > 0 && newInt != lastDisplayedInt) {
                    dashboardFlashPrice(displayPrice > lastRenderedPrice);
                    priceChangedSinceLastDraw = true;
                }
                lastRenderedPrice = displayPrice;
                lastDisplayedInt  = newInt;
                state.btc.usd = displayPrice;
                state.btc.valid = true;
                btcPriceAnim.set(displayPrice);
            }

            // Sync WS-derived sparkline every second (for periods using WS buffer)
            if (BTC_PERIODS[selectedPeriod].useWsBuf &&
                (curPair.source == PAIR_BINANCE_DIRECT || curPair.source == PAIR_BINANCE_INVERT)) {
                static unsigned long lastSparkSync = 0;
                unsigned long now = millis();
                if (now - lastSparkSync >= 1000) {
                    lastSparkSync = now;
                    SparklineData tmp;
                    wsBinanceGetSparkline(tmp);
                    if (tmp.valid) {
                        int trimTo = BTC_PERIODS[selectedPeriod].limit;
                        if (tmp.count > trimTo) {
                            int offset = tmp.count - trimTo;
                            tmp.minVal = 1e12f;
                            tmp.maxVal = -1e12f;
                            for (int i = 0; i < trimTo; i++) {
                                tmp.points[i] = tmp.points[offset + i];
                                if (tmp.points[i] < tmp.minVal) tmp.minVal = tmp.points[i];
                                if (tmp.points[i] > tmp.maxVal) tmp.maxVal = tmp.points[i];
                            }
                            tmp.count = trimTo;
                        }
                        state.spark = tmp;
                        wsVisualDirty = true;
                    }
                }
            }
        }

        // Stability mode: limit WS-driven hero redraw rate.
        {
            unsigned long now = millis();
            if (wsVisualDirty && (now - lastWsVisualDrawMs >= 2000)) {
                lastWsVisualDrawMs = now;
                wsVisualDirty = false;
                z1Dirty = true;
                frameDirty = true;
            }
        }

        scheduler.tick();

        // Drive morph animation (~30fps during 800ms transition)
        if (morphActive && chartStyle != CHART_CANDLE) {
            static unsigned long lastMorphFrame = 0;
            unsigned long now = millis();
            if (now - lastMorphFrame >= 33) {  // 30fps
                lastMorphFrame = now;
                z1Dirty = true;
            }
        }

        // Drive dollar morph animation (~30fps)
        if (dollarMorphActive) {
            static unsigned long lastDollarMorphFrame = 0;
            unsigned long now = millis();
            if (now - lastDollarMorphFrame >= 33) {
                lastDollarMorphFrame = now;
                SparklineData& morphed = getDollarMorphedSparkline();
                dashboardDrawLemonDollar(state.lemon, &morphed, dollarPeriod, dollarChartStyle, dollarChangePercent);
                z2DrawnThisFrame = true;
                z2Dirty = false;
            }
        }

        // Drive carousel momentum physics (~30fps)
        {
            static unsigned long lastCarouselFrame = 0;
            unsigned long now = millis();
            if (now - lastCarouselFrame >= 33) {
                float dt = (float)(now - lastCarouselFrame) / 1000.0f;
                lastCarouselFrame = now;

                if (btcCarousel.animating) {
                    updateCarouselPhysics(btcCarousel, BTC_PERIOD_COUNT, dt);
                    if (!btcCarousel.animating) {
                        // Snapped — clamp to pair's minimum and update
                        int snapped = (int)roundf(btcCarousel.scrollOffset);
                        if (snapped < 0) snapped = 0;
                        if (snapped >= BTC_PERIOD_COUNT) snapped = BTC_PERIOD_COUNT - 1;
                        uint8_t minIdx = BTC_PAIRS[selectedPair].minPeriodIdx;
                        if (snapped < (int)minIdx) snapped = (int)minIdx;
                        if (snapped != selectedPeriod) {
                            selectedPeriod = (uint8_t)snapped;
                            state.ohlc.valid = false;
                            if (chartStyle == CHART_CANDLE && !BTC_PERIODS[selectedPeriod].canOhlc) {
                                chartStyle = CHART_LINE;
                            }
                            scheduler.forceRun(taskSparkline);
                        }
                        btcCarousel.snappedIndex = snapped;
                    }
                    z1Dirty = true;
                    frameDirty = true;
                }

                if (dollarCarouselState.animating) {
                    updateCarouselPhysics(dollarCarouselState, DOLLAR_PERIOD_COUNT, dt);
                    if (!dollarCarouselState.animating) {
                        int snapped = (int)roundf(dollarCarouselState.scrollOffset);
                        if (snapped < 0) snapped = 0;
                        if (snapped >= DOLLAR_PERIOD_COUNT) snapped = DOLLAR_PERIOD_COUNT - 1;
                        if (snapped != dollarPeriod) {
                            dollarPeriod = (uint8_t)snapped;
                            dollarChangePercent = NAN;
                            scheduler.forceRun(taskDollarSpark);
                        }
                        dollarCarouselState.snappedIndex = snapped;
                    }
                    z2Dirty = true;
                    frameDirty = true;
                }

            }
        }

        // ── Consolidated zone rendering (max 1 push per zone per frame) ──
        if (z1Dirty && !z1DrawnThisFrame) {
            redrawHero();       // sets z1DrawnThisFrame, clears z1Dirty
            frameDirty = true;
        }
        if (z2Dirty && !z2DrawnThisFrame) {
            dashboardDrawLemonDollar(state.lemon,
                dollarMorphActive ? &getDollarMorphedSparkline() : &state.lemonSpark,
                dollarPeriod, dollarChartStyle, dollarChangePercent);
            z2DrawnThisFrame = true;
            z2Dirty = false;
            frameDirty = true;
        }
    }

    // Per-frame updates
    appTick();           // calls dashboardUpdateFlash() internally for SCREEN_DASHBOARD

    // Toast expiry: redraw header to restore normal content
    {
        static bool wasToastActive = false;
        bool toastNow = isToastActive();
        updateToast();
        bool toastAfter = isToastActive();
        // Toast just expired this frame
        if (wasToastActive && !toastAfter && screen == SCREEN_DASHBOARD) {
            String t = timeReady() ? getTimeStr(nvsGet24hFormat()) : String("--:--:--");
            dashboardDrawHeader(t.c_str(), !state.online, wsBinanceConnected());
            frameDirty = true;
        }
        wasToastActive = toastAfter;  // track for next frame
        (void)toastNow;
    }

    // Poll touch and dispatch
    TouchEvent evt = touchLoop();
    appHandleTouch(evt);

    // ── Frame pacing ──
    // Double buffering disabled (was causing bounce/repeat artifacts).
    // Single-buffer mode: sprites write directly to the framebuffer.
    // VSync wait reduces tearing by aligning draws to blanking period.
    if (screen == SCREEN_DASHBOARD) {
        if (frameDirty) {
            frameDirty = false;
        }
        delay(4);              // Yield to RTOS when idle
    } else {
        delay(20);
    }
}

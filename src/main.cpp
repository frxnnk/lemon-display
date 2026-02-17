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
static uint8_t selectedPeriod = 3;  // 0=15m, 1=1h, 2=24h, 3=7d, 4=30d, 5=1Y
static float periodChanges[6] = { NAN, NAN, NAN, NAN, NAN, NAN };
static ChartStyle chartStyle = CHART_LINE;
static ChartStyle dollarChartStyle = CHART_LINE;
static float dollarChangePercent = NAN;  // Pre-computed from real sparkline
static uint8_t dollarPeriod = 1;  // 0=24h, 1=7d, 2=30d, 3=1Y
static const int dollarPeriodDays[] = { 1, 7, 30, 365 };

// ── Animators ──
static ValueAnimator btcPriceAnim;
static ValueAnimator lemonBidAnim;
static ValueAnimator lemonAskAnim;
static SparklineAnimator sparkAnim;

// ── Scheduler & task IDs ──
static Scheduler scheduler;
static uint8_t taskClock, taskBtc, taskSparkline, taskLemon, taskDollarSpark;

// ── WS price dedup ──
static float lastRenderedPrice = 0.0f;

// ── Single-swap-per-frame flag ──
static bool frameDirty = false;

// ── Sparkline morph animation ──
#define MORPH_POINTS       100
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
    float ease = 1.0f - (inv * inv * inv);  // ease-out cubic

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
    float ease = 1.0f - (inv * inv * inv);  // ease-out cubic

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
    if (morphActive && chartStyle == CHART_LINE) {
        SparklineData& morphed = getMorphedSparkline();
        dashboardDrawBtcHero(state.btc, morphed, selectedPeriod, periodChanges, chartStyle, &state.ohlc);
    } else {
        dashboardDrawBtcHero(state.btc, state.spark, selectedPeriod, periodChanges, chartStyle, &state.ohlc);
    }
}

// ── Scheduled callbacks ──
static void updateClock() {
    if (timeReady()) {
        dashboardDrawHeader(getTimeStr().c_str(), !state.online, wsBinanceConnected());
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
        // Fill periodChanges from CoinGecko (indices shifted: 0=15m, 1=1h, 2=24h, 3=7d)
        periodChanges[1] = tmp.change1h;
        periodChanges[2] = tmp.change24h;
        periodChanges[3] = tmp.change7d;
        // Only update price from CoinGecko if WS has no price yet
        if (!wsBinanceHasPrice()) {
            state.btc.usd = tmp.usd;
            state.btc.valid = tmp.valid;
            btcPriceAnim.set(state.btc.usd);
        }
        redrawHero();
        frameDirty = true;
        if (nvsGetAlertEnabled() && fabsf(state.btc.change1h) >= ALERT_BTC_1H_THRESHOLD_PCT) {
            if (state.btc.change1h > 0) playAlertUp(); else playAlertDown();
        }
    }
}

static void updateSparkline() {
    if (!state.online) return;

    bool ok = false;
    // OHLC interval/limit map for candlestick mode
    const char* ohlcInterval = nullptr;
    int ohlcLimit = 0;

    if (selectedPeriod == 0) {
        // 15m: use WS circular buffer, trim to last 15 points
        wsBinanceGetSparkline(state.spark);
        if (state.spark.valid && state.spark.count > 15) {
            int offset = state.spark.count - 15;
            state.spark.minVal = 1e12f;
            state.spark.maxVal = -1e12f;
            for (int i = 0; i < 15; i++) {
                state.spark.points[i] = state.spark.points[offset + i];
                if (state.spark.points[i] < state.spark.minVal) state.spark.minVal = state.spark.points[i];
                if (state.spark.points[i] > state.spark.maxVal) state.spark.maxVal = state.spark.points[i];
            }
            state.spark.count = 15;
        }
        ok = state.spark.valid;
        // No OHLC for 15m (WS data only)
    } else if (selectedPeriod == 1) {
        // 1h: use WS circular buffer (1-minute candles)
        wsBinanceGetSparkline(state.spark);
        if (state.spark.valid && state.spark.count > 60) {
            int offset = state.spark.count - 60;
            state.spark.minVal = 1e12f;
            state.spark.maxVal = -1e12f;
            for (int i = 0; i < 60; i++) {
                state.spark.points[i] = state.spark.points[offset + i];
                if (state.spark.points[i] < state.spark.minVal) state.spark.minVal = state.spark.points[i];
                if (state.spark.points[i] > state.spark.maxVal) state.spark.maxVal = state.spark.points[i];
            }
            state.spark.count = 60;
        }
        ok = state.spark.valid;
        // No OHLC for 1h (WS data only)
    } else if (selectedPeriod == 2) {
        // 24h: Binance REST 15m klines
        ok = (fetchBinanceKlines(state.spark, "15m", 96) == API_OK);
        ohlcInterval = "15m"; ohlcLimit = 96;
    } else if (selectedPeriod == 3) {
        // 7d: Binance REST 2h klines, fallback to CoinGecko
        ok = (fetchBinanceKlines(state.spark, "2h", 84) == API_OK);
        if (!ok) ok = (fetchSparkline(state.spark, 7) == API_OK);
        ohlcInterval = "2h"; ohlcLimit = 84;
    } else if (selectedPeriod == 4) {
        // 30d: Binance REST 8h klines
        ok = (fetchBinanceKlines(state.spark, "8h", 90) == API_OK);
        ohlcInterval = "8h"; ohlcLimit = 90;
    } else if (selectedPeriod == 5) {
        // 1Y: Binance REST 1d klines (120 bars for readable candles)
        ok = (fetchBinanceKlines(state.spark, "1d", 365) == API_OK);
        ohlcInterval = "1d"; ohlcLimit = 120;
    }

    // Fetch OHLC data if in candle mode and interval is available
    if (ok && chartStyle == CHART_CANDLE && ohlcInterval) {
        fetchBinanceOhlc(state.ohlc, ohlcInterval, ohlcLimit);
    }

    if (ok) {
        // Disable sparkline morph animation on RGB panel to avoid tearing artifacts.
        morphActive = false;
        // Compute % change from sparkline first/last for periods 4 & 5
        if (selectedPeriod >= 4 && state.spark.count >= 2) {
            float first = state.spark.points[0];
            float last  = state.spark.points[state.spark.count - 1];
            if (first > 0) {
                periodChanges[selectedPeriod] = ((last - first) / first) * 100.0f;
            }
        }
        redrawHero();
        frameDirty = true;
    }
}

static void updateLemon() {
    if (!state.online) return;
    if (fetchLemonPrice(state.lemon) == API_OK) {
        lemonBidAnim.set(state.lemon.bid);
        lemonAskAnim.set(state.lemon.ask);
        dashboardDrawLemonDollar(state.lemon, &state.lemonSpark, dollarPeriod, dollarChartStyle, dollarChangePercent);
        frameDirty = true;
    }
}

static void updateDollarSparkline() {
    if (!state.online) return;

    if (fetchLemonSparkline(state.lemonSpark, dollarPeriodDays[dollarPeriod]) == API_OK) {
        // Pre-compute % change from real sparkline (morph won't corrupt it)
        if (state.lemonSpark.count >= 2) {
            float first = state.lemonSpark.points[0];
            float last = state.lemonSpark.points[state.lemonSpark.count - 1];
            // Sanity check: first must be >0 and within reasonable range of last
            // (CoinGecko sometimes returns anomalous near-zero points)
            if (first > 0 && last > 0 && first > last * 0.01f) {
                dollarChangePercent = ((last - first) / first) * 100.0f;
            } else {
                dollarChangePercent = NAN;
            }
        }
        // Disable dollar morph animation on RGB panel to avoid tearing artifacts.
        dollarMorphActive = false;
        dashboardDrawLemonDollar(state.lemon, &state.lemonSpark, dollarPeriod, dollarChartStyle, dollarChangePercent);
        frameDirty = true;
    }
}

// ── Dashboard touch callback ──
static const char* pLabels[] = { "15m", "1h", "24h", "7d", "30d", "1Y" };

static void onDashboardTouch(const TouchEvent& evt, uint8_t zoneId) {
    // ── Z1: BTC Hero — tap cycles chart style, carousel + swipe changes period ──
    if (zoneId == 1) {
        int8_t dir = 0;

        if (evt.gesture == TOUCH_TAP) {
            dir = dashboardHitTestCarousel(evt.x, evt.y);
        } else if (evt.gesture == TOUCH_SWIPE_LEFT) {
            dir = +1;  // Next period
        } else if (evt.gesture == TOUCH_SWIPE_RIGHT) {
            dir = -1;  // Previous period
        }

        if (dir != 0) {
            int8_t newPeriod = (int8_t)selectedPeriod + dir;
            if (newPeriod >= 0 && newPeriod <= 5) {
                selectedPeriod = (uint8_t)newPeriod;
                Serial.printf("[Touch] Period: %s\n", pLabels[selectedPeriod]);

                // Invalidate OHLC on period change
                state.ohlc.valid = false;

                // Auto-fallback from candle to line for 15m/1h
                if (chartStyle == CHART_CANDLE && selectedPeriod <= 1) {
                    chartStyle = CHART_LINE;
                    showToast("OHLC no disponible en WS");
                    // Redraw header to show toast in sprite
                    String t = timeReady() ? getTimeStr() : String("--:--:--");
                    dashboardDrawHeader(t.c_str(), !state.online, wsBinanceConnected());
                }

                redrawHero();
                frameDirty = true;
                scheduler.forceRun(taskSparkline);
            }
            return;
        }

        // Tap outside carousel: cycle chart style
        if (evt.gesture == TOUCH_TAP) {
            int newStyle = ((int)chartStyle + 1) % CHART_STYLE_COUNT;

            // Skip CHART_CANDLE for 15m/1h (periods 0,1) — no OHLC from WS
            if ((ChartStyle)newStyle == CHART_CANDLE && selectedPeriod <= 1) {
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

    // ── Z2: Dollar — carousel + swipe for period, tap outside = refresh ──
    if (zoneId == 2) {
        int8_t dir = 0;

        if (evt.gesture == TOUCH_TAP) {
            dir = dashboardHitTestDollarCarousel(evt.x, evt.y);
        } else if (evt.gesture == TOUCH_SWIPE_LEFT) {
            dir = +1;
        } else if (evt.gesture == TOUCH_SWIPE_RIGHT) {
            dir = -1;
        }

        if (dir != 0) {
            int8_t newP = (int8_t)dollarPeriod + dir;
            if (newP >= 0 && newP <= 3) {
                dollarPeriod = (uint8_t)newP;
                Serial.printf("[Touch] Dollar period: %dd\n", dollarPeriodDays[dollarPeriod]);
                dollarChangePercent = NAN;  // Will be recomputed from new sparkline
                dashboardDrawLemonDollar(state.lemon, &state.lemonSpark, dollarPeriod, dollarChartStyle, dollarChangePercent);
                scheduler.forceRun(taskDollarSpark);
            }
            return;
        }

        // Tap outside carousel: cycle chart style (LINE ↔ MARKERS, skip CANDLE)
        if (evt.gesture == TOUCH_TAP) {
            dollarChartStyle = (dollarChartStyle == CHART_LINE) ? CHART_MARKERS : CHART_LINE;
            Serial.printf("[Touch] Dollar chart style: %d\n", dollarChartStyle);
            dashboardDrawLemonDollar(state.lemon, &state.lemonSpark, dollarPeriod, dollarChartStyle, dollarChangePercent);
            frameDirty = true;
        }
        // Long press: force refresh
        else if (evt.gesture == TOUCH_LONG_PRESS) {
            Serial.println("[Touch] Force refresh: Lemon");
            dashboardStartFlash(2);
            dashboardDrawLemonDollar(state.lemon, &state.lemonSpark, dollarPeriod, dollarChartStyle, dollarChangePercent);  // Immediate flash border
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
    String timeStr = timeReady() ? getTimeStr() : String("--:--:--");
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
        periodChanges[1] = state.btc.change1h;
        periodChanges[2] = state.btc.change24h;
        periodChanges[3] = state.btc.change7d;
    }

    dashboardDrawLoading(LOAD_DOLLAR);
    Serial.println("[Main] Fetching Lemon dollar...");
    fetchLemonPrice(state.lemon);
    lemonBidAnim.set(state.lemon.bid);
    lemonAskAnim.set(state.lemon.ask);
    // Fetch initial dollar sparkline (default period)
    fetchLemonSparkline(state.lemonSpark, dollarPeriodDays[dollarPeriod]);
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

    String timeStr = getTimeStr();
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
    Serial.begin(115200);
    Serial.println("\n=== Lemon Interface v3.1 (MaTouch 480x480) ===");

    nvsInit();

    displaySetup();
    displaySetBrightness(nvsGetBrightness());

    dashboardSetup();
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
    AppScreen screen = appGetScreen();

    // ── WiFi QR Provisioning mode ──
    if (screen == SCREEN_WIFI_QR) {
        if (provisionTick()) {
            char ssid[33], pass[65];
            provisionGetCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
            nvsSaveWifi(ssid, pass);
            provisionStop();

            // Show connecting screen
            tft.fillScreen(Colors::BG_BASE);
            tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
            tft.setTextDatum(lgfx::middle_center);
            tft.drawString("Conectando...", SCREEN_W / 2, SCREEN_H / 2 - 10, &SatoshiMedium18);
            tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
            tft.drawString(ssid, SCREEN_W / 2, SCREEN_H / 2 + 20, &Satoshi12);
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
            dashboardDrawHeader(getTimeStr().c_str(), false, wsBinanceConnected());  // Toast bar
            frameDirty = true;
        }

        // WebSocket loop — must run every iteration
        wsBinanceLoop();

        // Check if WS has a new price to render
        if (wsBinanceHasPrice()) {
            float wsPrice = wsBinanceGetPrice();
            if (wsPrice != lastRenderedPrice) {
                // Trigger directional price flash before updating
                if (lastRenderedPrice > 0) {
                    dashboardFlashPrice(wsPrice > lastRenderedPrice);
                }
                lastRenderedPrice = wsPrice;
                state.btc.usd = wsPrice;
                state.btc.valid = true;
                btcPriceAnim.set(wsPrice);
                redrawHero();
                frameDirty = true;
            }

            // Sync 15m/1h sparkline from WS buffer every second
            if (selectedPeriod <= 1) {
                static unsigned long lastSparkSync = 0;
                unsigned long now = millis();
                if (now - lastSparkSync >= 1000) {
                    lastSparkSync = now;
                    SparklineData tmp;
                    wsBinanceGetSparkline(tmp);
                    if (tmp.valid) {
                        // Trim: 15 points for 15m, 60 for 1h
                        int trimTo = (selectedPeriod == 0) ? 15 : 60;
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
                    }
                }
            }
        }

        scheduler.tick();

        // Per-frame animation redraws disabled to keep RGB scanout stable.
        // Dashboard now redraws only on data/touch events.
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
            String t = timeReady() ? getTimeStr() : String("--:--:--");
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

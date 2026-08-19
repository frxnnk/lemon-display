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
#include "ui_stocks.h"
#include "config_server.h"
#include "ui_components.h"
#include "touch_manager.h"
#include "touch_utils.h"
#include "audio_manager.h"
#include "scheduler.h"
#include "animation.h"
#include "ws_binance.h"
#include "tutorial_overlay.h"
#include "ui_settings.h"
#include "ota_manager.h"
#include "app_control.h"
#include "ui_v2_demo.h"
#include "ui_usdt_mock.h"
#include "v2_runtime.h"
#include <esp_task_wdt.h>
#include <cmath>
#include <time.h>

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
static const uint8_t BTC_PERIODS_NORMAL[] = { 2, 4, 5, 7 };       // 1h, 24h, 1M, 1Y
static const uint8_t DOLLAR_PERIODS_NORMAL[] = { 0, 2, 4, 7 };    // 1d, 1w, 1M, 1Y
static uint8_t btcVisiblePeriods[BTC_PERIOD_COUNT] = {};
static uint8_t btcVisibleCount = 0;
static uint8_t dollarVisiblePeriods[DOLLAR_PERIOD_COUNT] = {};
static uint8_t dollarVisibleCount = 0;

// ── Polymarket prediction state ──
static PolyMarket polyMarkets[PM_MAX_MARKETS];
static uint8_t polyMarketCount = 0;
static uint8_t polySelectedIdx = 0;
static PolyPrediction activePreds[BTC_PERIOD_COUNT] = {};
static PolyStats polyStats = {};
static bool polyDataLoaded = false;
static bool  polyDeferredByStocks = false;
static char polyOutcomeMsg[BTC_PERIOD_COUNT][64] = {};
static char polyLoadStatus[64] = {};
static char polyResolveDebug[80] = {};  // last resolve-attempt status, shown in prediction UI
static const uint32_t POLY_EPOCH_TS_MIN = 1700000000UL;  // 2023-11-14 UTC
static const uint32_t POLY_ACTIVE_MAX_AGE_SEC = 20UL * 3600UL;  // short-term bets should settle quickly
static const uint32_t POLY_LOCAL_RESOLVE_GRACE_SEC = 30UL;

const char* polyGetResolveDebug() {
    return polyResolveDebug[0] ? polyResolveDebug : nullptr;
}

// ── Prediction history ring buffer ──
static PredHistoryEntry predHistory[PRED_HISTORY_MAX];
static uint8_t predHistHead = 0, predHistCount = 0;

static void predHistPush(const PredHistoryEntry& e) {
    predHistory[predHistHead] = e;
    predHistHead = (predHistHead + 1) % PRED_HISTORY_MAX;
    if (predHistCount < PRED_HISTORY_MAX) predHistCount++;
}

// ── Swipe animation state ──
static ValueAnimator predSwipeAnim;
static int predSwipeDir = 0;    // -1=swipe right, +1=swipe left, 0=none

// ── Countdown beep state (fire once per market) ──
static bool countdownBeepFired = false;

static PolyPrediction* activePredictionFor(uint8_t periodIdx) {
    return (periodIdx < BTC_PERIOD_COUNT) ? &activePreds[periodIdx] : nullptr;
}

static const PolyPrediction* currentActivePrediction() {
    if (selectedPeriod >= BTC_PERIOD_COUNT) return nullptr;
    return (activePreds[selectedPeriod].conditionId[0] != '\0') ? &activePreds[selectedPeriod] : nullptr;
}

static const PolyPrediction* firstOtherActivePrediction() {
    for (uint8_t i = 0; i < BTC_PERIOD_COUNT; i++) {
        if (i == selectedPeriod) continue;
        if (activePreds[i].conditionId[0] != '\0') return &activePreds[i];
    }
    return nullptr;
}

static const char* currentPolyOutcomeMsg() {
    if (polyOutcomeMsg[selectedPeriod][0]) return polyOutcomeMsg[selectedPeriod];
    return polyLoadStatus[0] ? polyLoadStatus : nullptr;
}

static uint32_t currentPolyStepSec() {
    switch (selectedPeriod) {
        case 0: return 300;    // 5m
        case 1: return 900;    // 15m
        case 2: return 3600;   // 1h
        case 3: return 14400;  // 4h
        default: return 300;
    }
}

static uint32_t polyStepSecForPeriod(uint8_t periodIdx) {
    switch (periodIdx) {
        case 0: return 300;
        case 1: return 900;
        case 2: return 3600;
        case 3: return 14400;
        default: return 0;
    }
}

// Helper: draw prediction UI with all current state (avoids repeating 11 args)
static void drawPredictionUI(bool loading) {
    const PolyPrediction* displayPred = currentActivePrediction();
    if (!displayPred) displayPred = firstOtherActivePrediction();
    dashboardDrawPrediction(polyMarkets, polyMarketCount, polySelectedIdx,
                            displayPred, polyStats, loading, currentPolyOutcomeMsg(),
                            state.btc.usd, currentPolyStepSec(), selectedPeriod,
                            predHistory, predHistHead, predHistCount);
}

static void loadPolyStatsForSelectedPeriod() {
    nvsLoadPolyStats(selectedPeriod, polyStats);
}

static bool refreshActivePredictionFromNvs(uint8_t periodIdx, bool clearLegacyTimestamp) {
    PolyPrediction* pred = activePredictionFor(periodIdx);
    if (!pred) return false;

    memset(pred, 0, sizeof(*pred));
    if (!nvsHasPolyPrediction(periodIdx)) return false;

    nvsLoadPolyPrediction(periodIdx, *pred);
    if (pred->conditionId[0] == '\0') {
        nvsClearPolyPrediction(periodIdx);
        return false;
    }

    if (clearLegacyTimestamp && pred->timestamp < POLY_EPOCH_TS_MIN) {
        Serial.printf("[Poly] Clearing legacy active prediction period=%u (ts=%lu)\n",
                      (unsigned)periodIdx, (unsigned long)pred->timestamp);
        nvsClearPolyPrediction(periodIdx);
        memset(pred, 0, sizeof(*pred));
        return false;
    }

    if (clearLegacyTimestamp && timeReady()) {
        time_t nowEpoch = time(nullptr);
        if (nowEpoch > (time_t)POLY_EPOCH_TS_MIN) {
            uint32_t nowTs = (uint32_t)nowEpoch;
            if (pred->timestamp == 0 || pred->timestamp > nowTs) {
                Serial.printf("[Poly] Clearing invalid active prediction period=%u ts=%lu now=%lu\n",
                              (unsigned)periodIdx, (unsigned long)pred->timestamp, (unsigned long)nowTs);
                nvsClearPolyPrediction(periodIdx);
                memset(pred, 0, sizeof(*pred));
                return false;
            }

            uint32_t ageSec = nowTs - pred->timestamp;
            if (ageSec > POLY_ACTIVE_MAX_AGE_SEC) {
                Serial.printf("[Poly] Clearing stale active prediction period=%u age=%lus\n",
                              (unsigned)periodIdx, (unsigned long)ageSec);
                nvsClearPolyPrediction(periodIdx);
                memset(pred, 0, sizeof(*pred));
                return false;
            }
        }
    }

    return true;
}

static void refreshAllActivePredictionsFromNvs(bool clearLegacyTimestamp) {
    for (uint8_t i = 0; i < BTC_PERIOD_COUNT; i++) {
        refreshActivePredictionFromNvs(i, clearLegacyTimestamp);
    }
}

static bool inferYesWinner(const PolyMarket& mkt, bool& yesWon) {
    // Only trust settled markets: a closed market pays 1.0 to the winning side
    // and 0.0 to the loser. Anything in between means Polymarket hasn't settled
    // yet — return false so the caller retries on the next tick instead of
    // locking in a premature winner.
    if (mkt.yesPrice >= 0.98f && mkt.noPrice <= 0.02f) {
        yesWon = true;
        return true;
    }
    if (mkt.noPrice >= 0.98f && mkt.yesPrice <= 0.02f) {
        yesWon = false;
        return true;
    }
    return false;
}

static bool inferYesWinnerFromBtcClose(const PolyPrediction& pred, bool& yesWon, float& endPriceOut) {
    endPriceOut = 0.0f;
    if (!timeReady() || pred.endEpoch == 0 || pred.refPrice <= 0.0f) return false;

    time_t nowEpoch = time(nullptr);
    if (nowEpoch < (time_t)(pred.endEpoch + POLY_LOCAL_RESOLVE_GRACE_SEC)) return false;

    ApiResult res = fetchBinanceOpenPriceAt(pred.endEpoch, endPriceOut);
    if (res != API_OK || endPriceOut <= 0.0f) return false;

    yesWon = endPriceOut > pred.refPrice;
    return true;
}

static void resolveActivePredictionIfClosed(uint8_t periodIdx) {
    if (!refreshActivePredictionFromNvs(periodIdx, true)) {
        // Quiet: no active bet to resolve.
        return;
    }

    PolyPrediction* pred = activePredictionFor(periodIdx);
    if (!pred) return;

    const char* hhmm = timeReady() ? getTimeStr(true) : "--:--";
    bool yesWon = false;
    float endPrice = 0.0f;
    static PolyMarket settled;  // static: ~265 bytes off the 8KB stack
    memset(&settled, 0, sizeof(settled));
    ApiResult res = fetchPolyMarketByConditionId(pred->conditionId, settled);
    if (res == API_OK) {
        Serial.printf("[Poly] Resolve check cond=%s closed=%d yes=%.3f no=%.3f end=%s\n",
                      pred->conditionId, settled.closed ? 1 : 0,
                      settled.yesPrice, settled.noPrice, settled.endDate);

        bool pricesTerminal = (settled.yesPrice >= 0.98f && settled.noPrice <= 0.02f) ||
                             (settled.noPrice  >= 0.98f && settled.yesPrice <= 0.02f);
        if (settled.winnerKnown) {
            yesWon = settled.yesWon;
        } else if (pricesTerminal && inferYesWinner(settled, yesWon)) {
        } else if (inferYesWinnerFromBtcClose(*pred, yesWon, endPrice)) {
            snprintf(polyResolveDebug, sizeof(polyResolveDebug),
                     "Chk %s btc=%.0f ref=%.0f", hhmm, endPrice, pred->refPrice);
        } else {
            if (settled.closed) {
                Serial.printf("[Poly] Resolve waiting: closed but no winner yet (yes=%.3f no=%.3f)\n",
                              settled.yesPrice, settled.noPrice);
                snprintf(polyResolveDebug, sizeof(polyResolveDebug),
                         "Chk %s closed y=%.2f n=%.2f", hhmm,
                         settled.yesPrice, settled.noPrice);
            } else {
                Serial.println("[Poly] Resolve skipped: not settled yet");
                snprintf(polyResolveDebug, sizeof(polyResolveDebug),
                         "Chk %s c=%d y=%.2f n=%.2f", hhmm, settled.closed ? 1 : 0,
                         settled.yesPrice, settled.noPrice);
            }
            return;
        }
    } else if (inferYesWinnerFromBtcClose(*pred, yesWon, endPrice)) {
        snprintf(polyResolveDebug, sizeof(polyResolveDebug),
                 "Chk %s btc=%.0f ref=%.0f", hhmm, endPrice, pred->refPrice);
    } else {
        Serial.printf("[Poly] Resolve fetch failed cond=%s res=%d\n",
                      pred->conditionId, (int)res);
        snprintf(polyResolveDebug, sizeof(polyResolveDebug),
                 "Chk %s: fetch err %d", hhmm, (int)res);
        return;
    }
    polyResolveDebug[0] = '\0';  // clear once we actually resolve

    bool userWon = (pred->chosenYes == yesWon);
    uint8_t statsPeriod = (pred->periodIdx < BTC_PERIOD_COUNT)
        ? pred->periodIdx
        : selectedPeriod;

    PolyStats resolvedStats = {};
    nvsLoadPolyStats(statsPeriod, resolvedStats);
    if (userWon) {
        resolvedStats.wins++;
        resolvedStats.streak++;
        if (resolvedStats.streak > resolvedStats.bestStreak) {
            resolvedStats.bestStreak = resolvedStats.streak;
        }
    } else {
        resolvedStats.losses++;
        resolvedStats.streak = 0;
    }
    nvsSavePolyStats(statsPeriod, resolvedStats);

    if (statsPeriod == selectedPeriod) {
        polyStats = resolvedStats;
    }

    snprintf(polyOutcomeMsg[statsPeriod], sizeof(polyOutcomeMsg[statsPeriod]),
             "%s (%s gano)",
             userWon ? "Ganaste!" : "Perdiste",
             yesWon ? "SUBE" : "BAJA");

    // Update prediction history: find the pending entry and set result
    for (uint8_t i = 0; i < predHistCount; i++) {
        int idx = ((int)predHistHead - 1 - i + PRED_HISTORY_MAX) % PRED_HISTORY_MAX;
        if (predHistory[idx].result == 0 &&
            predHistory[idx].periodIdx == statsPeriod &&
            predHistory[idx].timestamp == pred->timestamp) {
            predHistory[idx].result = userWon ? 1 : 2;
            break;
        }
    }
    nvsSavePredHistory(predHistory, predHistHead, predHistCount);

    // Sound feedback on resolution
    if (audioIsEnabled()) {
        userWon ? playAlertUp() : playAlertDown();
    }

    char toastBuf[48];
    snprintf(toastBuf, sizeof(toastBuf), "%s - gano %s",
             userWon ? "Ganaste" : "Perdiste",
             yesWon ? "SUBE" : "BAJA");
    showToast(toastBuf);
    // Header redraw deferred — toast rendering loop in main loop will pick it up

    char resolvedCond[PM_COND_ID_LEN];
    strncpy(resolvedCond, pred->conditionId, sizeof(resolvedCond) - 1);
    resolvedCond[sizeof(resolvedCond) - 1] = '\0';
    nvsClearPolyPrediction(statsPeriod);
    memset(pred, 0, sizeof(*pred));
    Serial.printf("[Poly] Resolved cond=%s -> winner=%s user=%s period=%u\n",
                  resolvedCond, yesWon ? "YES" : "NO", userWon ? "WIN" : "LOSE",
                  (unsigned)statsPeriod);
}

// Cancel the active prediction without counting it. Used when a stuck pending
// bet blocks new bets (e.g. a market that closed with indecisive prices so
// inferYesWinner never accepted settlement). Marks the history entry as 3
// (cancelled) so the user can see it was discarded.
static void cancelPendingPrediction() {
    if (!refreshActivePredictionFromNvs(selectedPeriod, true)) {
        showToast("Sin apuesta activa");
        return;
    }

    PolyPrediction* pred = activePredictionFor(selectedPeriod);
    if (!pred) {
        showToast("Sin apuesta activa");
        return;
    }

    for (uint8_t i = 0; i < predHistCount; i++) {
        int idx = ((int)predHistHead - 1 - i + PRED_HISTORY_MAX) % PRED_HISTORY_MAX;
        if (predHistory[idx].result == 0 &&
            predHistory[idx].periodIdx == selectedPeriod &&
            predHistory[idx].timestamp == pred->timestamp) {
            predHistory[idx].result = 3;  // cancelled
            break;
        }
    }
    nvsSavePredHistory(predHistory, predHistHead, predHistCount);

    nvsClearPolyPrediction(selectedPeriod);
    memset(pred, 0, sizeof(*pred));
    polyOutcomeMsg[selectedPeriod][0] = '\0';
    showToast("Apuesta cancelada");
    Serial.println("[Poly] Pending prediction cancelled by user");
}

// Debug helper: force-resolve the active prediction as a WIN or LOSS without
// waiting for Polymarket to settle. Triggered by a double-tap on the stats card.
static void debugForceResolve(bool userWon) {
    if (!refreshActivePredictionFromNvs(selectedPeriod, true)) {
        showToast("Sin apuesta activa");
        return;
    }

    PolyPrediction* pred = activePredictionFor(selectedPeriod);
    if (!pred) {
        showToast("Sin apuesta activa");
        return;
    }

    bool yesWon = userWon ? pred->chosenYes : !pred->chosenYes;
    uint8_t statsPeriod = (pred->periodIdx < BTC_PERIOD_COUNT)
        ? pred->periodIdx
        : selectedPeriod;

    PolyStats resolvedStats = {};
    nvsLoadPolyStats(statsPeriod, resolvedStats);
    if (userWon) {
        resolvedStats.wins++;
        resolvedStats.streak++;
        if (resolvedStats.streak > resolvedStats.bestStreak) {
            resolvedStats.bestStreak = resolvedStats.streak;
        }
    } else {
        resolvedStats.losses++;
        resolvedStats.streak = 0;
    }
    nvsSavePolyStats(statsPeriod, resolvedStats);

    if (statsPeriod == selectedPeriod) {
        polyStats = resolvedStats;
    }

    snprintf(polyOutcomeMsg[statsPeriod], sizeof(polyOutcomeMsg[statsPeriod]),
             "%s (%s gano) [SIM]",
             userWon ? "Ganaste!" : "Perdiste",
             yesWon ? "SUBE" : "BAJA");

    for (uint8_t i = 0; i < predHistCount; i++) {
        int idx = ((int)predHistHead - 1 - i + PRED_HISTORY_MAX) % PRED_HISTORY_MAX;
        if (predHistory[idx].result == 0 &&
            predHistory[idx].periodIdx == statsPeriod &&
            predHistory[idx].timestamp == pred->timestamp) {
            predHistory[idx].result = userWon ? 1 : 2;
            break;
        }
    }
    nvsSavePredHistory(predHistory, predHistHead, predHistCount);

    if (audioIsEnabled()) {
        userWon ? playAlertUp() : playAlertDown();
    }

    char toastBuf[48];
    snprintf(toastBuf, sizeof(toastBuf), "[SIM] %s",
             userWon ? "Ganaste" : "Perdiste");
    showToast(toastBuf);

    nvsClearPolyPrediction(statsPeriod);
    memset(pred, 0, sizeof(*pred));
    Serial.printf("[Poly] SIM resolved user=%s period=%u\n",
                  userWon ? "WIN" : "LOSE", (unsigned)statsPeriod);
}

// ── Animators ──
static ValueAnimator btcPriceAnim;
static ValueAnimator lemonBidAnim;
static ValueAnimator lemonAskAnim;
static SparklineAnimator sparkAnim;

// ── Scheduler & task IDs ──
Scheduler scheduler;  // non-static: ui_stocks references it via extern
static uint8_t taskClock, taskBtc, taskSparkline, taskLemon, taskDollarSpark, taskCrossRate, taskPolymarket;
uint8_t taskStocks;  // non-static: ui_stocks references it to schedule off-thread refreshes

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
static float morphOldMin, morphOldMax;
static uint32_t morphStartMs;
static bool morphActive = false;
static SparklineData morphedResult;

// ── Dollar sparkline morph animation ──
static float dollarMorphOldNorm[MORPH_POINTS];
static float dollarMorphNewNorm[MORPH_POINTS];
static float dollarMorphNewMin, dollarMorphNewMax;
static float dollarMorphOldMin, dollarMorphOldMax;
static uint32_t dollarMorphStartMs;
static bool dollarMorphActive = false;
static SparklineData dollarMorphedResult;

// ── Forward declarations ──
static void enterDashboard();
static void tryConnectSavedWifi();
static void startProvisioning();
static void exitPredictionMode();
static void switchPair(uint8_t newPair);
static void refreshPredictionForCurrentPeriod(bool showLoading);
static void redrawDashboard();
static void cycleZ2Mode(int8_t dir);
static void applyZ2Mode(Z2Mode target);
static void requestStocksBurst();

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
    if (src.count < 2 || count < 2) {
        float val = (src.count >= 1) ? 0.5f : 0.0f;
        for (int i = 0; i < count; i++) dst[i] = val;
        return;
    }
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
    float min = morphOldMin + ease * (morphNewMin - morphOldMin);
    float max = morphOldMax + ease * (morphNewMax - morphOldMax);
    float range = max - min;
    if (range < 0.01f) range = 1.0f;
    morphedResult.minVal = min;
    morphedResult.maxVal = max;

    for (int i = 0; i < MORPH_POINTS; i++) {
        float norm = morphOldNorm[i] + ease * (morphNewNorm[i] - morphOldNorm[i]);
        morphedResult.points[i] = min + norm * range;
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
    float min = dollarMorphOldMin + ease * (dollarMorphNewMin - dollarMorphOldMin);
    float max = dollarMorphOldMax + ease * (dollarMorphNewMax - dollarMorphOldMax);
    float range = max - min;
    if (range < 0.01f) range = 1.0f;
    dollarMorphedResult.minVal = min;
    dollarMorphedResult.maxVal = max;

    for (int i = 0; i < MORPH_POINTS; i++) {
        float norm = dollarMorphOldNorm[i] + ease * (dollarMorphNewNorm[i] - dollarMorphOldNorm[i]);
        dollarMorphedResult.points[i] = min + norm * range;
    }
    dollarMorphedResult.valid = true;
    return dollarMorphedResult;
}

// ── Helper: redraw hero with current state (uses morph if active) ──
static bool isProModeEnabled() {
    return nvsGetProMode();
}

static void stabilizeLiveSparklineRange(SparklineData& next, const SparklineData& prev) {
    if (!next.valid || next.count < 2 || !prev.valid || prev.count < 2) return;
    if (!isfinite(next.minVal) || !isfinite(next.maxVal) ||
        !isfinite(prev.minVal) || !isfinite(prev.maxVal)) return;
    if (next.maxVal <= next.minVal || prev.maxVal <= prev.minVal) return;

    float rawMin = next.minVal;
    float rawMax = next.maxVal;
    float pad = (rawMax - rawMin) * 0.08f;
    if (pad < 0.5f) pad = 0.5f;

    float targetMin = rawMin - pad;
    float targetMax = rawMax + pad;

    if (rawMin >= prev.minVal && rawMax <= prev.maxVal) {
        const float shrink = 0.08f;
        next.minVal = prev.minVal + (targetMin - prev.minVal) * shrink;
        next.maxVal = prev.maxVal + (targetMax - prev.maxVal) * shrink;
        if (next.minVal > rawMin) next.minVal = rawMin;
        if (next.maxVal < rawMax) next.maxVal = rawMax;
    } else {
        next.minVal = fminf(prev.minVal, targetMin);
        next.maxVal = fmaxf(prev.maxVal, targetMax);
    }

    if (next.maxVal - next.minVal < 0.01f) {
        next.minVal = rawMin;
        next.maxVal = rawMax;
    }
}

static void requestStocksBurst() {
    apiStop();
    delay(100);
    stocksRequestBurst();
}

static bool pairSupportsCandles(const PairDef& pair) {
    return pair.restSymbol &&
           (pair.source == PAIR_BINANCE_DIRECT || pair.source == PAIR_BINANCE_INVERT);
}

static bool currentSelectionSupportsCandles() {
    if (selectedPeriod >= BTC_PERIOD_COUNT || selectedPair >= BTC_PAIR_COUNT) return false;

    const PeriodDef& pd = BTC_PERIODS[selectedPeriod];
    const PairDef& pair = BTC_PAIRS[selectedPair];
    return pd.canOhlc && pd.klineInterval && pairSupportsCandles(pair);
}

static int findNearestVisibleCandlePeriod() {
    if (selectedPair >= BTC_PAIR_COUNT || !pairSupportsCandles(BTC_PAIRS[selectedPair])) return -1;

    int bestPeriod = -1;
    int bestDist = 999;
    for (uint8_t i = 0; i < btcVisibleCount; i++) {
        uint8_t periodIdx = btcVisiblePeriods[i];
        if (periodIdx >= BTC_PERIOD_COUNT) continue;

        const PeriodDef& pd = BTC_PERIODS[periodIdx];
        if (!pd.canOhlc || !pd.klineInterval) continue;

        int dist = abs((int)periodIdx - (int)selectedPeriod);
        if (dist < bestDist || (dist == bestDist && periodIdx > selectedPeriod)) {
            bestPeriod = periodIdx;
            bestDist = dist;
        }
    }
    return bestPeriod;
}

// Periods with Polymarket up/down markets (only 5m and 15m exist)
static const uint8_t POLY_PERIODS[] = { 0, 1 };

static void buildVisibleBtcPeriods() {
    uint8_t minIdx = BTC_PAIRS[selectedPair].minPeriodIdx;
    btcVisibleCount = 0;

    if (dashboardIsPredictionMode()) {
        // Prediction mode: only periods with Polymarket games
        for (uint8_t i = 0; i < (uint8_t)(sizeof(POLY_PERIODS) / sizeof(POLY_PERIODS[0])); i++) {
            uint8_t idx = POLY_PERIODS[i];
            if (idx >= minIdx) {
                btcVisiblePeriods[btcVisibleCount++] = idx;
            }
        }
    } else if (isProModeEnabled()) {
        for (uint8_t i = 0; i < BTC_PERIOD_COUNT; i++) {
            if (i >= minIdx) {
                btcVisiblePeriods[btcVisibleCount++] = i;
            }
        }
    } else {
        const uint8_t normalCount = (uint8_t)(sizeof(BTC_PERIODS_NORMAL) / sizeof(BTC_PERIODS_NORMAL[0]));
        for (uint8_t i = 0; i < normalCount; i++) {
            uint8_t idx = BTC_PERIODS_NORMAL[i];
            if (idx >= minIdx) {
                btcVisiblePeriods[btcVisibleCount++] = idx;
            }
        }
    }

    if (btcVisibleCount == 0) {
        btcVisiblePeriods[0] = (minIdx < BTC_PERIOD_COUNT) ? minIdx : (uint8_t)(BTC_PERIOD_COUNT - 1);
        btcVisibleCount = 1;
    }
}

static void buildVisibleDollarPeriods() {
    if (isProModeEnabled()) {
        // Skip index 5 (3M / 90 days) — CoinGecko returns ~196KB hourly data
        // which exceeds the 96KB API response buffer, leaving the chart stuck.
        dollarVisibleCount = 0;
        for (uint8_t i = 0; i < DOLLAR_PERIOD_COUNT; i++) {
            if (i == 5) continue;
            dollarVisiblePeriods[dollarVisibleCount++] = i;
        }
    } else {
        dollarVisibleCount = (uint8_t)(sizeof(DOLLAR_PERIODS_NORMAL) / sizeof(DOLLAR_PERIODS_NORMAL[0]));
        for (uint8_t i = 0; i < dollarVisibleCount; i++) dollarVisiblePeriods[i] = DOLLAR_PERIODS_NORMAL[i];
    }
}

static int findVisibleSlot(const uint8_t* arr, uint8_t count, uint8_t realIdx) {
    for (uint8_t i = 0; i < count; i++) {
        if (arr[i] == realIdx) return (int)i;
    }
    return -1;
}

static uint8_t clampToNearestAllowed(uint8_t current, const uint8_t* arr, uint8_t count) {
    if (!arr || count == 0) return current;

    uint8_t best = arr[0];
    int bestDist = abs((int)best - (int)current);
    for (uint8_t i = 1; i < count; i++) {
        int dist = abs((int)arr[i] - (int)current);
        if (dist < bestDist || (dist == bestDist && arr[i] > best)) {
            best = arr[i];
            bestDist = dist;
        }
    }
    return best;
}

static void syncDashboardFilters() {
    buildVisibleBtcPeriods();
    buildVisibleDollarPeriods();
    dashboardSetPairSelectorEnabled(isProModeEnabled());
    dashboardSetBtcCarouselFilter(btcVisiblePeriods, btcVisibleCount, selectedPeriod);
    dashboardSetDollarCarouselFilter(dollarVisiblePeriods, dollarVisibleCount, dollarPeriod);
}

static void applyModePolicyNow(bool forceSparkRefresh = false) {
    bool pro = isProModeEnabled();

    buildVisibleBtcPeriods();
    buildVisibleDollarPeriods();

    if (!pro) {
        if (dashboardIsPairDropdownOpen()) {
            dashboardClosePairDropdown();
        }
        if (dashboardIsPredictionMode()) {
            exitPredictionMode();
        }
        if (selectedPair != 0) {
            switchPair(0);
        }
    }

    uint8_t clampedBtc = clampToNearestAllowed(selectedPeriod, btcVisiblePeriods, btcVisibleCount);
    if (clampedBtc != selectedPeriod) {
        selectedPeriod = clampedBtc;
        state.ohlc.valid = false;
        if (chartStyle == CHART_CANDLE && !currentSelectionSupportsCandles()) {
            chartStyle = CHART_LINE;
        }
        if (forceSparkRefresh) {
            scheduler.forceRun(taskSparkline);
            refreshPredictionForCurrentPeriod(true);
        }
    }

    uint8_t clampedDollar = clampToNearestAllowed(dollarPeriod, dollarVisiblePeriods, dollarVisibleCount);
    if (clampedDollar != dollarPeriod) {
        dollarPeriod = clampedDollar;
        dollarChangePercent = NAN;
        if (forceSparkRefresh) {
            scheduler.requestRun(taskDollarSpark);
        }
    }

    syncDashboardFilters();
}

static int currentBtcSlot() {
    int slot = findVisibleSlot(btcVisiblePeriods, btcVisibleCount, selectedPeriod);
    if (slot < 0) {
        selectedPeriod = clampToNearestAllowed(selectedPeriod, btcVisiblePeriods, btcVisibleCount);
        slot = findVisibleSlot(btcVisiblePeriods, btcVisibleCount, selectedPeriod);
    }
    return (slot >= 0) ? slot : 0;
}

static int currentDollarSlot() {
    int slot = findVisibleSlot(dollarVisiblePeriods, dollarVisibleCount, dollarPeriod);
    if (slot < 0) {
        dollarPeriod = clampToNearestAllowed(dollarPeriod, dollarVisiblePeriods, dollarVisibleCount);
        slot = findVisibleSlot(dollarVisiblePeriods, dollarVisibleCount, dollarPeriod);
    }
    return (slot >= 0) ? slot : 0;
}
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
        // Skip direct framebuffer writes while tutorial overlay or toast is active
        // (toast replaces the entire header — time writes would corrupt it)
        // Also skip during BTC morph — morph driver handles Z1 at 30fps,
        // direct writes would push stale sprite clips between morph frames.
        if (!tutorialIsActive() && !isToastActive() && !morphActive) {
            dashboardUpdateTimeDirect(getTimeStr(nvsGet24hFormat()));
            if (priceChangedSinceLastDraw && !z1Dirty) {
                priceChangedSinceLastDraw = false;
                dashboardUpdatePriceDirect(state.btc, selectedPair);
            }
        }
        frameDirty = true;
    }
}

static void updateBtc() {
    if (!state.online) return;
    if (stocksIsFetching()) return;
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
        // Avoid full 288KB push — only price (via direct) needs live update.
        // % change text updates on next natural full redraw (morph, touch, etc.)
        priceChangedSinceLastDraw = true;
        frameDirty = true;
        if (nvsGetAlertEnabled() && fabsf(state.btc.change1h) >= ALERT_BTC_1H_THRESHOLD_PCT) {
            if (state.btc.change1h > 0) playAlertUp(); else playAlertDown();
        }
    }
}

static void updateSparkline() {
    if (!state.online) return;
    if (stocksIsFetching()) return;
    esp_task_wdt_reset();

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

        // Fetch OHLC data if in candle mode and the pair/period supports it
        if (ok && chartStyle == CHART_CANDLE && currentSelectionSupportsCandles()) {
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
            morphOldMin = oldSpark.minVal;
            morphOldMax = oldSpark.maxVal;
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
    if (stocksIsFetching()) return;
    if (fetchLemonPrice(state.lemon) == API_OK) {
        lemonBidAnim.set(state.lemon.bid);
        lemonAskAnim.set(state.lemon.ask);

        // Update crossRate if ARS pair is active
        if (BTC_PAIRS[selectedPair].source == PAIR_DERIVED) {
            crossRate = (state.lemon.bid + state.lemon.ask) / 2.0f;
            priceChangedSinceLastDraw = true;
        }

        // Skip redraw during morph — the morph driver handles Z2 at 30fps
        if (!dollarMorphActive && dashboardGetZ2H() > 0) {
            z2Dirty = true;
        }
        frameDirty = true;
    }
}

static void updateDollarSparkline() {
    if (!state.online || dashboardGetZ2H() <= 0) return;
    if (stocksIsFetching()) return;
    esp_task_wdt_reset();

    // Save old sparkline for morph (and as fallback if fetch fails)
    static SparklineData oldDollarSpark;
    oldDollarSpark = state.lemonSpark;

    ApiResult res = fetchLemonSparkline(state.lemonSpark, DOLLAR_PERIODS[dollarPeriod].days);

    if (res == API_OK && state.lemonSpark.valid && state.lemonSpark.count >= 2) {
        // Pre-compute % change from real sparkline (morph won't corrupt it)
        float first = state.lemonSpark.points[0];
        float last = state.lemonSpark.points[state.lemonSpark.count - 1];
        if (first > 0 && last > 0 && first > last * 0.01f) {
            dollarChangePercent = ((last - first) / first) * 100.0f;
        } else {
            dollarChangePercent = NAN;
        }
        // Start dollar morph animation
        if (oldDollarSpark.valid && oldDollarSpark.count >= 2) {
            dollarMorphOldMin = oldDollarSpark.minVal;
            dollarMorphOldMax = oldDollarSpark.maxVal;
            resampleNormalize(oldDollarSpark, dollarMorphOldNorm, MORPH_POINTS);
            resampleNormalize(state.lemonSpark, dollarMorphNewNorm, MORPH_POINTS);
            dollarMorphNewMin = state.lemonSpark.minVal;
            dollarMorphNewMax = state.lemonSpark.maxVal;
            dollarMorphStartMs = millis();
            dollarMorphActive = true;
        } else {
            z2Dirty = true;
        }
        frameDirty = true;
    } else {
        // Fetch failed or invalid — restore old sparkline so chart doesn't break
        state.lemonSpark = oldDollarSpark;
        Serial.printf("[API] Dollar sparkline fetch failed (res=%d) — keeping old data\n", (int)res);
        z2Dirty = true;
        frameDirty = true;
    }
}

// ── Cross-rate update (for DERIVED and GECKO_ONLY pairs) ──
static void updateCrossRate() {
    if (!state.online) return;
    if (stocksIsFetching()) return;
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

// ── Polymarket update callback ──
static void updatePolymarket() {
    if (!state.online) return;
    if (stocksIsFetching()) {
        polyDeferredByStocks = true;
        strncpy(polyLoadStatus, "Esperando Acciones...", sizeof(polyLoadStatus) - 1);
        polyLoadStatus[sizeof(polyLoadStatus) - 1] = '\0';
        if (dashboardIsPredictionMode()) {
            drawPredictionUI(true);
            z2DrawnThisFrame = true;
            z2Dirty = false;
            frameDirty = true;
        }
        Serial.println("[Poly] Deferred while Stocks fetch is active");
        return;
    }
    polyDeferredByStocks = false;

    // Resolve any pending prediction first — runs regardless of current screen so
    // wins/losses register as soon as the underlying Polymarket settles.
    esp_task_wdt_reset();
    for (uint8_t i = 0; i < (uint8_t)(sizeof(POLY_PERIODS) / sizeof(POLY_PERIODS[0])); i++) {
        resolveActivePredictionIfClosed(POLY_PERIODS[i]);
        esp_task_wdt_reset();
    }

    if (!dashboardIsPredictionMode()) return;

    uint8_t cnt = 0;
    Serial.printf("[Poly] Fetch for period idx=%d (%s)\n",
                  selectedPeriod, BTC_PERIODS[selectedPeriod].label);
    esp_task_wdt_reset();
    ApiResult res = fetchPolyMarkets(polyMarkets, cnt, PM_MAX_MARKETS, selectedPeriod);

    if (res == API_OK && cnt > 0) {
        polyLoadStatus[0] = '\0';
        polyMarketCount = cnt;
        if (polySelectedIdx >= polyMarketCount) polySelectedIdx = 0;
        polyDataLoaded = true;
        const PolyMarket& dbg = polyMarkets[polySelectedIdx];
        Serial.printf("[Poly] Active market: %s | yes=%.3f no=%.3f | end=%s\n",
                      dbg.question, dbg.yesPrice, dbg.noPrice, dbg.endDate);

        // Phase 1: draw immediately with current BTC price as approximate threshold
        drawPredictionUI(false);
        z2DrawnThisFrame = true;
        z2Dirty = false;
        frameDirty = true;

        // Phase 2: enrich with Chainlink reference price (skip if already cached)
        for (uint8_t i = 0; i < polyMarketCount; i++) {
            if (polyMarkets[i].refPriceValid) continue;  // Skip if cached
            esp_task_wdt_reset();
            enrichPolyReference(polyMarkets[i]);
        }

        // Redraw with real threshold if enrichment succeeded
        if (polyMarkets[polySelectedIdx].refPriceValid) {
            drawPredictionUI(false);
            z2DrawnThisFrame = true;
            z2Dirty = false;
            frameDirty = true;
        }
    }
    else {
        snprintf(polyLoadStatus, sizeof(polyLoadStatus), "Error %d - reintentando", (int)res);
        polyMarketCount = 0;
        polySelectedIdx = 0;
        polyDataLoaded = false;
        drawPredictionUI(false);
        z2DrawnThisFrame = true;
        z2Dirty = false;
        frameDirty = true;
        Serial.printf("[Poly] Update failed: res=%d period=%s\n",
                      (int)res, BTC_PERIODS[selectedPeriod].label);
    }
    Serial.printf("[Poly] Update: %d markets, selected=%d\n", polyMarketCount, polySelectedIdx);
}

// ── Enter/exit prediction mode ──
static void enterPredictionMode() {
    if (dashboardGetZ2H() <= 0) {
        showToast("Cambia a BTC + USD");
        const char* t = timeReady() ? getTimeStr(nvsGet24hFormat()) : "--:--:--";
        dashboardDrawHeader(t, !state.online, wsBinanceConnected());
        frameDirty = true;
        return;
    }
    if (!isProModeEnabled()) {
        showToast("Modo Pro para Polymarket");
        const char* t = timeReady() ? getTimeStr(nvsGet24hFormat()) : "--:--:--";
        dashboardDrawHeader(t, !state.online, wsBinanceConnected());
        frameDirty = true;
        return;
    }

    dashboardSetPredictionMode(true);
    polyDataLoaded = false;
    polySelectedIdx = 0;

    // Lock to BTC/USD in prediction mode
    if (selectedPair != 0) switchPair(0);

    // Filter carousel to only periods with Polymarket games
    syncDashboardFilters();

    // Disable pair selector AFTER syncDashboardFilters (which resets it to isProModeEnabled)
    dashboardSetPairSelectorEnabled(false);

    // Clamp period to nearest available prediction period
    {
        uint8_t clamped = clampToNearestAllowed(selectedPeriod, btcVisiblePeriods, btcVisibleCount);
        if (clamped != selectedPeriod) selectedPeriod = clamped;
    }

    // Resize zones: Z1 shrinks, Z2 expands
    dashboardSetPredictionLayout(true);

    loadPolyStatsForSelectedPeriod();
    nvsLoadPredHistory(predHistory, predHistHead, predHistCount);
    refreshAllActivePredictionsFromNvs(true);
    countdownBeepFired = false;

    dashboardBeginBatch();
    redrawHero();
    drawPredictionUI(true);
    dashboardCommitBatch();

    z2DrawnThisFrame = true;
    z2Dirty = false;
    frameDirty = true;

    // Enable scheduler task — runs on next loop tick (non-blocking)
    scheduler.enable(taskPolymarket, true);
    scheduler.requestRun(taskPolymarket);

    Serial.println("[Poly] Prediction mode entered (Z2)");
}

static void exitPredictionMode() {
    dashboardSetPredictionMode(false);
    scheduler.setInterval(taskPolymarket, POLYMARKET_REFRESH_MS);

    dashboardSetPairSelectorEnabled(isProModeEnabled());
    syncDashboardFilters();

    dashboardSetPredictionLayout(false);

    redrawDashboard();

    Serial.println("[Poly] Prediction mode exited");
}

// ── Z2 mode cycling (USD ↔ Markets ↔ Stocks) ──
// Driven by vertical swipes on Z2. Handles the side-effects of entering /
// leaving Markets (prediction mode has a different layout and activates
// its own scheduler task).
static void applyZ2Mode(Z2Mode target) {
    bool wasPrediction = dashboardIsPredictionMode();
    Z2Mode prev = dashboardGetZ2Mode();
    if (target == prev && wasPrediction == (target == Z2_MARKETS)) return;

    // If we're leaving MARKETS, tear down prediction mode first.
    if (wasPrediction && target != Z2_MARKETS) {
        exitPredictionMode();
    }

    // Stocks scheduler task follows the slot mode — only runs while the
    // user is actually viewing stocks. Saves Yahoo quota and keeps the
    // main-loop HTTPS burst off first-boot when default mode is USD.
    bool stocksOn = (target == Z2_STOCKS);
    stocksSetActive(stocksOn);
    scheduler.enable(taskStocks, stocksOn);
    // Burst-refresh every watchlist symbol on mode entry so charts fill in
    // ~N×1-3s instead of N×60s of round-robin. Scheduler's 60s poll kicks
    // in normally after the burst finishes.
    if (stocksOn) requestStocksBurst();

    if (target == Z2_MARKETS) {
        // Prediction mode has guards (Pro-mode, z2H>0). If they fail, it
        // shows a toast and returns — we stay on the previous mode.
        if (!isProModeEnabled() || dashboardGetZ2H() <= 0) {
            enterPredictionMode();     // shows toast, returns without flipping
            return;                     // keep previous Z2 mode
        }
        dashboardSetZ2Mode(Z2_MARKETS);
        enterPredictionMode();
        return;
    }

    dashboardSetZ2Mode(target);
    redrawDashboard();
}

void appApplyZ2Mode(Z2Mode mode) {
    applyZ2Mode(mode);
}

static void cycleZ2Mode(int8_t dir) {
    Z2Mode cur = dashboardGetZ2Mode();
    int8_t n = (int8_t)cur + dir;
    while (n < 0) n += (int8_t)Z2_COUNT;
    n %= (int8_t)Z2_COUNT;
    applyZ2Mode((Z2Mode)n);
}

// ── Place prediction ──
static void placePrediction(bool chooseYes) {
    if (!polyDataLoaded || polyMarketCount == 0) return;

    const PolyMarket& mkt = polyMarkets[polySelectedIdx];
    if (mkt.closed || mkt.winnerKnown) {
        showToast("Mercado cerrado");
        return;
    }
    if (!mkt.refPriceValid || mkt.refPrice <= 0.0f) {
        showToast("Espera el umbral");
        return;
    }
    uint32_t endEpoch = isoToEpoch(mkt.endDate);
    if (endEpoch == 0) {
        showToast("Fin invalido");
        return;
    }

    // Refuse to overwrite a pending bet — previously the old prediction was
    // silently cleared from NVS, losing the W/L entirely.
    if (refreshActivePredictionFromNvs(selectedPeriod, true)) {
        PolyPrediction* pred = activePredictionFor(selectedPeriod);
        if (pred && strcmp(pred->conditionId, mkt.conditionId) == 0) {
            showToast("Ya apostaste en este mercado");
            return;
        }
        showToast("Ya tenes apuesta activa en este timeframe");
        return;
    }

    time_t nowEpoch = time(nullptr);
    if (!timeReady() || nowEpoch <= (time_t)POLY_EPOCH_TS_MIN) {
        showToast("Espera la hora");
        return;
    }

    PolyPrediction* pred = activePredictionFor(selectedPeriod);
    if (!pred) return;
    memset(pred, 0, sizeof(*pred));
    strncpy(pred->conditionId, mkt.conditionId, PM_COND_ID_LEN - 1);
    pred->chosenYes = chooseYes;
    pred->probAtBet = chooseYes ? mkt.yesPrice : mkt.noPrice;
    pred->refPrice = mkt.refPrice;
    pred->endEpoch = endEpoch;
    pred->periodIdx = selectedPeriod;
    pred->timestamp = (uint32_t)nowEpoch;
    pred->resolved = 0;

    polyOutcomeMsg[selectedPeriod][0] = '\0';
    nvsSavePolyPrediction(selectedPeriod, *pred);

    // Push to prediction history
    PredHistoryEntry histEntry = {};
    histEntry.timestamp = pred->timestamp;
    histEntry.periodIdx = pred->periodIdx;
    histEntry.chosenYes = pred->chosenYes;
    histEntry.probAtBet = pred->probAtBet;
    histEntry.result = 0;  // pending
    predHistPush(histEntry);
    nvsSavePredHistory(predHistory, predHistHead, predHistCount);

    // Sound feedback
    if (audioIsEnabled()) playPredictionPlaced();

    showToast(chooseYes ? "Prediccion: SUBE" : "Prediccion: BAJA");
    const char* t = timeReady() ? getTimeStr(nvsGet24hFormat()) : "--:--:--";
    dashboardDrawHeader(t, !state.online, wsBinanceConnected());

    drawPredictionUI(false);
    z2DrawnThisFrame = true;
    z2Dirty = false;
    frameDirty = true;

    Serial.printf("[Poly] Prediction placed: %s @ %.0f%%\n",
                  chooseYes ? "YES" : "NO", pred->probAtBet * 100);
}

// ── Refresh prediction market when BTC timeframe changes ──
static void refreshPredictionForCurrentPeriod(bool showLoading = true) {
    if (!dashboardIsPredictionMode()) return;

    loadPolyStatsForSelectedPeriod();
    polyDataLoaded = false;
    polySelectedIdx = 0;

    if (showLoading) {
        drawPredictionUI(true);
        z2DrawnThisFrame = true;
        z2Dirty = false;
        frameDirty = true;
    } else {
        z2Dirty = true;
        frameDirty = true;
    }

    scheduler.requestRun(taskPolymarket);  // Non-blocking — runs on next loop tick
    Serial.printf("[Poly] Refresh for period: %s (idx=%d)\n",
                  BTC_PERIODS[selectedPeriod].label, selectedPeriod);
}

// ── Switch active trading pair ──
static void switchPair(uint8_t newPair) {
    if (newPair >= BTC_PAIR_COUNT) return;
    if (!isProModeEnabled() && newPair != 0) return;
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

    if (chartStyle == CHART_CANDLE && (!pairSupportsCandles(pair) || !currentSelectionSupportsCandles())) {
        chartStyle = CHART_LINE;
    }

    // Reset period changes
    for (int i = 0; i < BTC_PERIOD_COUNT; i++) periodChanges[i] = NAN;

    // ── Immediate visual update: show new pair label, loading state ──
    // Invalidate price — WS still has old pair data, don't show stale number
    state.btc.valid = false;
    redrawHero();
    frameDirty = true;

    // ── Network: reconnect WS + backfill (safe with 16KB stack) ──
    esp_task_wdt_reset();
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
    esp_task_wdt_reset();
    syncDashboardFilters();
    scheduler.forceRun(taskSparkline);
    esp_task_wdt_reset();
    refreshPredictionForCurrentPeriod(true);
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
            int curSlot = currentBtcSlot();
            int newSlot = curSlot + dir;
            if (newSlot < 0) newSlot = 0;
            if (newSlot >= (int)btcVisibleCount) newSlot = (int)btcVisibleCount - 1;
            if (newSlot >= 0 && newSlot < (int)btcVisibleCount && newSlot != curSlot) {
                selectedPeriod = btcVisiblePeriods[newSlot];
                Serial.printf("[Touch] Period: %s\n", BTC_PERIODS[selectedPeriod].label);

                state.ohlc.valid = false;
                periodChanges[selectedPeriod] = NAN;

                if (chartStyle == CHART_CANDLE && !currentSelectionSupportsCandles()) {
                    chartStyle = CHART_LINE;
                    showToast("Velas no disponible");
                    const char* t = timeReady() ? getTimeStr(nvsGet24hFormat()) : "--:--:--";
                    dashboardDrawHeader(t, !state.online, wsBinanceConnected());
                }

                redrawHero();
                frameDirty = true;
                esp_task_wdt_reset();
                scheduler.forceRun(taskSparkline);
                esp_task_wdt_reset();
                refreshPredictionForCurrentPeriod(true);
            }
            return;
        }

        // Tap outside both carousels: cycle chart style
        if (evt.gesture == TOUCH_TAP) {
            int newStyle = ((int)chartStyle + 1) % CHART_STYLE_COUNT;
            bool candlePeriodChanged = false;

            if ((ChartStyle)newStyle == CHART_CANDLE && !currentSelectionSupportsCandles()) {
                int candlePeriod = findNearestVisibleCandlePeriod();
                if (candlePeriod >= 0) {
                    if ((uint8_t)candlePeriod != selectedPeriod) {
                        selectedPeriod = (uint8_t)candlePeriod;
                        state.ohlc.valid = false;
                        periodChanges[selectedPeriod] = NAN;
                        syncDashboardFilters();
                        candlePeriodChanged = true;
                        Serial.printf("[Touch] Period: %s\n", BTC_PERIODS[selectedPeriod].label);
                    }
                } else {
                    newStyle = (newStyle + 1) % CHART_STYLE_COUNT;
                    showToast("Velas no disponible");
                    const char* t = timeReady() ? getTimeStr(nvsGet24hFormat()) : "--:--:--";
                    dashboardDrawHeader(t, !state.online, wsBinanceConnected());
                }
            }

            chartStyle = (ChartStyle)newStyle;
            Serial.printf("[Touch] Chart style: %d\n", chartStyle);

            if (chartStyle == CHART_CANDLE && !state.ohlc.valid) {
                scheduler.forceRun(taskSparkline);
                if (candlePeriodChanged) {
                    refreshPredictionForCurrentPeriod(true);
                }
            }

            redrawHero();
        }
        // Long press: force refresh
        else if (evt.gesture == TOUCH_LONG_PRESS) {
            Serial.println("[Touch] Force refresh: BTC + Sparkline");
            dashboardStartFlash(1);
            redrawHero();           // Immediate redraw to show flash border
            frameDirty = true;
            esp_task_wdt_reset();
            scheduler.forceRun(taskBtc);
            esp_task_wdt_reset();
            scheduler.forceRun(taskSparkline);
        }
        return;
    }

    // ── Z2: Dollar / Markets / Stocks ──
    if (zoneId == 2) {
        // Vertical gestures on Z2 cycle between the three Z2 modes
        // (USD / Markets / Stocks). Slower swipes come through as
        // TOUCH_SWIPE_UP/DOWN; flicks are reclassified by the touch
        // manager as TOUCH_FLING_UP/DOWN — handle both.
        if (evt.gesture == TOUCH_SWIPE_UP || evt.gesture == TOUCH_FLING_UP) {
            cycleZ2Mode(+1);
            return;
        }
        if (evt.gesture == TOUCH_SWIPE_DOWN || evt.gesture == TOUCH_FLING_DOWN) {
            cycleZ2Mode(-1);
            return;
        }

        // ── Stocks mode: tap or horizontal swipe advances focused ticker ──
        if (!dashboardIsPredictionMode() && dashboardGetZ2Mode() == Z2_STOCKS) {
            if (evt.gesture == TOUCH_TAP ||
                evt.gesture == TOUCH_SWIPE_LEFT ||
                evt.gesture == TOUCH_SWIPE_RIGHT) {
                stocksAdvanceFocused();
                dashboardDrawStocksZ2();
                frameDirty = true;
            }
            return;
        }

        // ── Prediction mode: handle all Z2 touches differently ──
        if (dashboardIsPredictionMode()) {
            if (evt.gesture == TOUCH_TAP) {
                if (dashboardHitTestPredYes(evt.x, evt.y)) {
                    placePrediction(true);
                    return;
                }
                if (dashboardHitTestPredNo(evt.x, evt.y)) {
                    placePrediction(false);
                    return;
                }
            }
            // Swipe left/right: browse markets with slide animation
            if (evt.gesture == TOUCH_SWIPE_LEFT && polyMarketCount > 1) {
                predSwipeDir = 1;
                predSwipeAnim.set(1.0f);
                predSwipeAnim.setTarget(0.0f, 250);
                polySelectedIdx = (polySelectedIdx + 1) % polyMarketCount;
                countdownBeepFired = false;
                drawPredictionUI(false);
                z2DrawnThisFrame = true;
                z2Dirty = false;
                frameDirty = true;
                return;
            }
            if (evt.gesture == TOUCH_SWIPE_RIGHT && polyMarketCount > 1) {
                predSwipeDir = -1;
                predSwipeAnim.set(1.0f);
                predSwipeAnim.setTarget(0.0f, 250);
                polySelectedIdx = (polySelectedIdx == 0) ? polyMarketCount - 1 : polySelectedIdx - 1;
                countdownBeepFired = false;
                drawPredictionUI(false);
                z2DrawnThisFrame = true;
                z2Dirty = false;
                frameDirty = true;
                return;
            }
            // Long press on stats card: cancel a stuck pending bet without counting
            if (evt.gesture == TOUCH_LONG_PRESS &&
                dashboardHitTestPredStats(evt.x, evt.y)) {
                cancelPendingPrediction();
                drawPredictionUI(false);
                z2DrawnThisFrame = true;
                z2Dirty = false;
                frameDirty = true;
                return;
            }
            // Long press elsewhere: exit prediction mode
            if (evt.gesture == TOUCH_LONG_PRESS) {
                exitPredictionMode();
                return;
            }
            return;  // Swallow all other gestures in prediction mode
        }

        // ── Normal dollar mode ──
        // Vertical swipes were handled above (Z2 mode cycle). Horizontal
        // swipes and the carousel tap still move through dollar periods.
        int8_t dir = 0;

        if (evt.gesture == TOUCH_TAP) {
            dir = dashboardHitTestDollarCarousel(evt.x, evt.y);
        } else if (evt.gesture == TOUCH_SWIPE_LEFT) {
            dir = +1;
        } else if (evt.gesture == TOUCH_SWIPE_RIGHT) {
            dir = -1;
        }

        if (dir != 0) {
            int curSlot = currentDollarSlot();
            int newSlot = curSlot + dir;
            if (newSlot < 0) newSlot = 0;
            if (newSlot >= (int)dollarVisibleCount) newSlot = (int)dollarVisibleCount - 1;
            if (newSlot >= 0 && newSlot < (int)dollarVisibleCount && newSlot != curSlot) {
                dollarPeriod = dollarVisiblePeriods[newSlot];
                Serial.printf("[Touch] Dollar period: %s\n", DOLLAR_PERIODS[dollarPeriod].label);
                dollarChangePercent = NAN;
                // Show old sparkline briefly — morph will animate to new data once fetched.
                // Avoids black chart gap that occurred with nullptr.
                dashboardDrawLemonDollar(state.lemon, &state.lemonSpark, dollarPeriod, dollarChartStyle, dollarChangePercent);
                z2DrawnThisFrame = true;
                z2Dirty = false;
                frameDirty = true;
                esp_task_wdt_reset();
                scheduler.forceRun(taskDollarSpark);
                z2Dirty = true;
                z2DrawnThisFrame = false;
            }
            return;
        }

        // Tap outside carousel: cycle chart style (LINE <-> MARKERS, skip CANDLE)
        if (evt.gesture == TOUCH_TAP) {
            dollarChartStyle = (dollarChartStyle == CHART_LINE) ? CHART_MARKERS : CHART_LINE;
            Serial.printf("[Touch] Dollar chart style: %d\n", dollarChartStyle);
            dashboardDrawLemonDollar(state.lemon, &state.lemonSpark, dollarPeriod, dollarChartStyle, dollarChangePercent);
            z2DrawnThisFrame = true;
            z2Dirty = false;
            frameDirty = true;
        }
        // Long press: enter prediction mode
        else if (evt.gesture == TOUCH_LONG_PRESS) {
            if (!isProModeEnabled()) {
                showToast("Modo Pro para Polymarket");
                const char* t = timeReady() ? getTimeStr(nvsGet24hFormat()) : "--:--:--";
                dashboardDrawHeader(t, !state.online, wsBinanceConnected());
                frameDirty = true;
                return;
            }
            Serial.println("[Touch] Long press on Z2 -> prediction mode");
            enterPredictionMode();
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
    // TOUCH_DOUBLE_TAP on zone 0 (header) is intercepted at the ui_views
    // layer — it cycles through the views carousel regardless of which view
    // is active, so every view benefits from the same gesture.
}

// ── Redraw dashboard ──
static void redrawDashboard() {
    applyModePolicyNow(true);
    const char* timeStr = timeReady() ? getTimeStr(nvsGet24hFormat()) : "--:--:--";
    dashboardDrawAll(timeStr,
                     state.btc, state.spark, selectedPeriod,
                     selectedPair,
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
    configServerStart();   // app config HTTP server on device IP (port 80)

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

    applyModePolicyNow(false);
    const char* timeStr = getTimeStr(nvsGet24hFormat());
    dashboardDrawAll(timeStr,
                     state.btc, state.spark, selectedPeriod,
                     selectedPair,
                     state.lemon, !state.online, wsBinanceConnected(),
                     periodChanges, chartStyle, &state.ohlc, &state.lemonSpark,
                     dollarPeriod, dollarChartStyle, dollarChangePercent);

    // Double buffering disabled — was causing vertical bounce/repeat artifacts.
    // All draws go through sprites (toast, flash in sprite) so tearing is minimal.
    // dbuf::enable(tft, dashboardSyncDrawBuffer);

    appSetScreen(SCREEN_DASHBOARD);
    tutorialInit();  // Show tutorial on first boot (checks NVS)

    // If the user booted directly into Stocks mode (persisted from last
    // session), kick the burst refresh now that WiFi is up. Otherwise the
    // cached NVS sparks render immediately and the scheduler tick would
    // only refresh the first symbol 60s later.
    if (dashboardGetZ2Mode() == Z2_STOCKS) {
        stocksSetActive(true);
        requestStocksBurst();
    }
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

#if LEMON_V2_DEMO_MODE
    Serial.begin(115200);
    Serial.println("\n=== Lemon Box V2 offline demo ===");
    displaySetup();
    displaySetupVSync();
    touchSetup();
    v2DemoSetup();
    return;
#endif

#if LEMON_USDT_MOCK_MODE
    Serial.begin(115200);
    Serial.println("\n=== Lemon USDT Control Room mock ===");
    displaySetup();
    displaySetupVSync();
    touchSetup();
    usdtMockSetup();
    return;
#endif

#if LEMON_V2_REAL_MODE
    Serial.begin(115200);
    Serial.println("\n=== Lemon Box V2 real canary ===");
    nvsInit();
    Colors::setTheme(Colors::THEME_DARK);
    displaySetup();
    displaySetupVSync();
    displaySetBrightness(nvsGetBrightness());
    audioSetup();
    audioSetEnabled(nvsGetSoundEnabled());
    touchSetup();
    v2RuntimeSetup();
    esp_task_wdt_init(45, true);
    esp_task_wdt_add(NULL);
    return;
#endif

    Serial.begin(115200);
    Serial.println("\n=== Lemon Interface v4.0 (MaTouch 480x480) ===");

    // Initialize period changes to NAN
    for (int i = 0; i < BTC_PERIOD_COUNT; i++) periodChanges[i] = NAN;

    nvsInit();
    Colors::setTheme(nvsGetTheme() == 1 ? Colors::THEME_LIGHT : Colors::THEME_DARK);

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
    stocksInit();

    taskClock     = scheduler.add("clock",     UPDATE_CLOCK_MS,      updateClock);
    taskBtc       = scheduler.add("btc",       UPDATE_BTC_PRICE_MS,  updateBtc);
    taskSparkline = scheduler.add("sparkline", UPDATE_SPARKLINE_MS,  updateSparkline);
    taskLemon     = scheduler.add("lemon",     UPDATE_LEMON_MS,      updateLemon);
    taskDollarSpark = scheduler.add("dolarSpark", UPDATE_SPARKLINE_MS, updateDollarSparkline);
    taskCrossRate   = scheduler.add("crossRate", 60000, updateCrossRate);  // 60s for XAU/ARS rates
    taskPolymarket  = scheduler.add("polymarket", POLYMARKET_REFRESH_MS, updatePolymarket);
    // Task stays enabled at the default interval so pending predictions resolve
    // in the background; the callback gates the market fetch on prediction mode.
    taskStocks      = scheduler.add("stocks",    UPDATE_STOCKS_MS,     stocksFetchTask);
    // Stocks stays idle until the user swaps into Stocks mode — otherwise the
    // first-boot burst of HTTPS fetches blocks the main loop and the UI feels
    // trabada. applyZ2Mode enables/disables as the user cycles.
    {
        bool stocksOn = dashboardGetZ2Mode() == Z2_STOCKS;
        stocksSetActive(stocksOn);
        scheduler.enable(taskStocks, stocksOn);
    }

    applyModePolicyNow(false);

    // ── Boot flow ──
    if (nvsHasWifi()) {
        dashboardDrawLoading(LOAD_WIFI);
        tryConnectSavedWifi();
    } else {
        Serial.println("[Main] No WiFi configured — starting QR provisioning");
        startProvisioning();
    }

    if (audioIsEnabled()) playStartup();
    Serial.println("[Main] Ready");

    esp_task_wdt_init(45, true);  // 45s timeout — scheduler resets WDT between tasks now
    esp_task_wdt_add(NULL);        // Subscribe loopTask
}

void loop() {
#if DISPLAY_DIAG_MODE
    delay(20);
    return;
#endif

#if LEMON_V2_DEMO_MODE
    v2DemoTick(millis());
    TouchEvent demoTouch = touchLoop();
    v2DemoHandleTouch(demoTouch);
    delay(4);
    return;
#endif

#if LEMON_USDT_MOCK_MODE
    usdtMockTick(millis());
    TouchEvent mockTouch = touchLoop();
    usdtMockHandleTouch(mockTouch);
    delay(4);
    return;
#endif

#if LEMON_V2_REAL_MODE
    esp_task_wdt_reset();
    v2RuntimeLoop();
    return;
#endif

    esp_task_wdt_reset();

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
        dashboardSetDeferred(true);

        if (!state.online && !state.wasOffline) {
            wsBinanceStop();
            state.wasOffline = true;
            showToast("WiFi desconectado");
            if (!tutorialIsActive()) {
                dashboardDrawHeader("--:--:--", true);
            }
            frameDirty = true;
        } else if (state.online && state.wasOffline) {
            state.wasOffline = false;
            timeSetup();
            wsBinanceSetup();
            if (dashboardGetZ2Mode() == Z2_STOCKS) {
                stocksSetActive(true);
                scheduler.enable(taskStocks, true);
                requestStocksBurst();
            }
            showToast("WiFi reconectado");
            if (!tutorialIsActive()) {
                dashboardDrawHeader(getTimeStr(nvsGet24hFormat()), false, wsBinanceConnected());
            }
            frameDirty = true;
        }

        if (state.online && settingsConsumeServiceRecovery()) {
            const PairDef& pair = BTC_PAIRS[selectedPair];
            if (pair.source == PAIR_BINANCE_DIRECT || pair.source == PAIR_BINANCE_INVERT) {
                wsBinanceReconnect(pair.wsPath, pair.inverted);
            } else {
                wsBinanceReconnect(BTC_PAIRS[0].wsPath, false);
            }
            if (dashboardGetZ2Mode() == Z2_STOCKS) {
                stocksInit();
                stocksSetActive(true);
                scheduler.enable(taskStocks, true);
                requestStocksBurst();
            }
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
                    static SparklineData tmp;  // static: 1,476 bytes off the 8KB stack
                    wsBinanceGetSparkline(tmp);
                    if (tmp.valid) {
                        static uint8_t lastLiveSparkPeriod = 255;
                        static uint8_t lastLiveSparkPair = 255;
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
                        if (lastLiveSparkPeriod == selectedPeriod && lastLiveSparkPair == selectedPair) {
                            stabilizeLiveSparklineRange(tmp, state.spark);
                        }
                        lastLiveSparkPeriod = selectedPeriod;
                        lastLiveSparkPair = selectedPair;
                        state.spark = tmp;
                        wsVisualDirty = true;
                    }
                }
            }
        }

        // WS-driven chart redraw: clipped partial push (chart area only).
        // Uses dashboardRedrawChartOnly which redraws chart in sprite then
        // pushes only the chart clip rect (~60% less data than full Z1).
        {
            unsigned long now = millis();
            unsigned long interval = (chartStyle == CHART_MARKERS) ? 5000 : 2000;
            if (wsVisualDirty && !tutorialIsActive() && (now - lastWsVisualDrawMs >= interval)) {
                lastWsVisualDrawMs = now;
                wsVisualDirty = false;
                if (!morphActive) {
                    dashboardRedrawChartOnly(state.spark, chartStyle, &state.ohlc, state.btc.ath);
                    z1DrawnThisFrame = true;
                    z1Dirty = false;
                }
                frameDirty = true;
            }
        }

        scheduler.tick();

        // ── Stocks async worker → UI repaint ──
        // The worker runs on core 0 and mutates s_quotes / s_sparks after
        // a Yahoo fetch; it can't touch the TFT itself, so we poll
        // stocksConsumeDirty() here and redraw Z2 from the main loop.
        if (stocksConsumeDirty() &&
            appGetScreen() == SCREEN_DASHBOARD &&
            !dashboardIsPredictionMode() &&
            dashboardGetZ2Mode() == Z2_STOCKS &&
            dashboardGetZ2H() > 0) {
            dashboardDrawStocksZ2();
            frameDirty = true;
        }

        if (polyDeferredByStocks && !stocksIsFetching()) {
            polyDeferredByStocks = false;
            scheduler.requestRun(taskPolymarket);
            if (dashboardIsPredictionMode()) {
                drawPredictionUI(true);
                z2DrawnThisFrame = true;
                z2Dirty = false;
                frameDirty = true;
            }
            Serial.println("[Poly] Deferred fetch resumed after Stocks idle");
        }

        // ── Prediction countdown tick (1Hz Z2 refresh + adaptive polling + beep) ──
        if (dashboardIsPredictionMode() && polyDataLoaded && !tutorialIsActive()) {
            static unsigned long lastCountdownTick = 0;
            unsigned long now = millis();
            if (now - lastCountdownTick >= 1000) {
                lastCountdownTick = now;

                uint32_t endEp = dashboardGetPredEndEpoch();
                if (endEp > 0) {
                    time_t nowEpoch = time(nullptr);
                    int32_t remainSec = (int32_t)(endEp - (uint32_t)nowEpoch);

                    // Adaptive polling: speed up near market close
                    unsigned long newInterval = POLYMARKET_REFRESH_MS;
                    if (remainSec <= 0)       newInterval = 3000;
                    else if (remainSec <= 15) newInterval = 3000;
                    else if (remainSec <= 60) newInterval = 5000;
                    scheduler.setInterval(taskPolymarket, newInterval);

                    // Countdown beep when market closes
                    if (remainSec <= 0 && !countdownBeepFired) {
                        countdownBeepFired = true;
                        if (audioIsEnabled()) playCountdownEnd();
                    }
                }

                // Direct partial update — only redraws countdown strip (no flicker)
                dashboardUpdateCountdownDirect();
            }
        }

        // Drive morph animation (~30fps during 800ms transition)
        // Chart-only push for intermediate frames (~40% less data than full Z1).
        // Morph needs immediate push (not deferred) for smooth 30fps animation.
        {
            static bool wasMorphing = false;
            if (morphActive && chartStyle != CHART_CANDLE && !tutorialIsActive()) {
                static unsigned long lastMorphFrame = 0;
                unsigned long now = millis();
                if (now - lastMorphFrame >= 33) {
                    lastMorphFrame = now;
                    dashboardFlushDeferred();
                    dashboardSetDeferred(false);
                    SparklineData& morphed = getMorphedSparkline();
                    dashboardRedrawChartOnly(morphed, chartStyle, &state.ohlc, state.btc.ath);
                    dashboardSetDeferred(true);
                    z1DrawnThisFrame = true;
                    z1Dirty = false;
                    frameDirty = true;
                    if (!morphActive) {
                        z1Dirty = true;
                        z1DrawnThisFrame = false;
                    }
                }
            }
            wasMorphing = morphActive;
        }

        // Drive dollar morph animation (~30fps) — skip in prediction mode, tutorial, and BTC-only layout
        if (dollarMorphActive && dashboardGetZ2H() > 0 && !dashboardIsPredictionMode() && !tutorialIsActive()) {
            static unsigned long lastDollarMorphFrame = 0;
            unsigned long now = millis();
            if (now - lastDollarMorphFrame >= 33) {
                lastDollarMorphFrame = now;
                SparklineData& morphed = getDollarMorphedSparkline();
                dashboardRedrawDollarChartOnly(state.lemon, morphed, dollarChartStyle);
                z2DrawnThisFrame = true;
                z2Dirty = false;
                // Final frame: redraw with real data to show % change
                if (!dollarMorphActive) {
                    z2Dirty = true;
                    z2DrawnThisFrame = false;
                }
            }
        }

        // Drive prediction swipe animation (~30fps during 250ms slide)
        if (predSwipeAnim.isAnimating() && dashboardIsPredictionMode() && !tutorialIsActive()) {
            static unsigned long lastSwipeFrame = 0;
            unsigned long now = millis();
            if (now - lastSwipeFrame >= 33) {
                lastSwipeFrame = now;
                predSwipeAnim.update();
                // Redraw prediction UI (animation complete redraws final state)
                drawPredictionUI(false);
                z2DrawnThisFrame = true;
                z2Dirty = false;
                frameDirty = true;
            }
        }

        // ── Consolidated zone rendering (max 1 push per zone per frame) ──
        // Skip zone rendering while tutorial overlay is active (prevents flicker)
        if (!tutorialIsActive()) {
            if (z1Dirty && !z1DrawnThisFrame) {
                redrawHero();       // sets z1DrawnThisFrame, clears z1Dirty
                frameDirty = true;
            }
            if (z2Dirty && !z2DrawnThisFrame && dashboardGetZ2H() > 0) {
                if (dashboardIsPredictionMode()) {
                    drawPredictionUI(!polyDataLoaded);
                } else {
                    dashboardDrawLemonDollar(state.lemon,
                        dollarMorphActive ? &getDollarMorphedSparkline() : &state.lemonSpark,
                        dollarPeriod, dollarChartStyle, dollarChangePercent);
                }
                z2DrawnThisFrame = true;
                z2Dirty = false;
                frameDirty = true;
            }
        }
    }

    // Per-frame updates
    appTick();           // calls dashboardUpdateFlash() internally for SCREEN_DASHBOARD

    // Toast lifecycle: redraw header on toast appear and expire
    {
        static bool wasToastActive = false;
        bool toastNow = isToastActive();
        updateToast();
        bool toastAfter = isToastActive();

        if (screen == SCREEN_DASHBOARD && !tutorialIsActive()) {
            // Toast just appeared OR just expired → redraw header
            if (toastNow != wasToastActive || (wasToastActive && !toastAfter)) {
                const char* t = timeReady() ? getTimeStr(nvsGet24hFormat()) : "--:--:--";
                dashboardDrawHeader(t, !state.online, wsBinanceConnected());
                frameDirty = true;
            }
        }
        wasToastActive = toastAfter;
    }

    // ── Tutorial overlay (draws only on step change, not every frame) ──
    {
        tutorialCheckPending();  // Start queued pro tutorial now that we're on dashboard
        if (tutorialNeedsReset()) {
            selectedPeriod = 4;  // Reset to 24h for clean tutorial visuals
            redrawDashboard();
        }
        static bool wasTutActive = false;
        if (tutorialIsActive()) {
            if (tutorialNeedsDraw()) {
                // tutorialDraw() handles everything: dim + clipped spotlight push + tooltip slide-in
                tutorialDraw();
                frameDirty = true;
            }
        } else if (wasTutActive) {
            // Tutorial just ended — full screen recovery:
            // Fill gaps between zones (tooltip/dimming artifacts live there)
            dashboardFillGaps();
            // Clear stale dirty flags accumulated while tutorial blocked zone rendering
            z1Dirty = false;
            z2Dirty = false;
            z1DrawnThisFrame = true;  // Prevent zone rendering from re-drawing this frame
            z2DrawnThisFrame = true;
            // Full redraw of all zones
            redrawDashboard();
        }
        wasTutActive = tutorialIsActive();
    }

    // Poll touch and dispatch
    TouchEvent evt = touchLoop();
    if (tutorialIsActive()) {
        tutorialHandleTouch(evt);
    } else {
        appHandleTouch(evt);
    }

    // ── Frame pacing ──
    if (screen == SCREEN_DASHBOARD) {
        dashboardFlushDeferred();
        if (frameDirty) {
            frameDirty = false;
        }
        delay(4);
    } else {
        delay(20);
    }
}

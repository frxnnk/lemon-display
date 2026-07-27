#include "v2_runtime.h"

#include "audio_manager.h"
#include "config.h"
#include "config_server.h"
#include "display_manager.h"
#include "nvs_storage.h"
#include "ota_manager.h"
#include "scheduler.h"
#include "time_manager.h"
#include "ui_v2_runtime.h"
#include "news_client.h"
#include "wifi_manager.h"
#include "wifi_provision.h"
#include "ws_binance.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cstdint>
#include <cmath>
#include <esp_task_wdt.h>

extern Scheduler scheduler;
extern uint8_t taskStocks;

static V2RuntimeModel s_model;
static V2RuntimeSnapshot s_snapshot;
static bool s_provisioning = false;
static bool s_networkStarted = false;
static bool s_dirty = true;
static uint8_t s_drawnScene = 0xFF;
static uint32_t s_lastClockDrawMs = 0;
static uint32_t s_lastBtcAttemptMs = 0;
static uint32_t s_lastLemonAttemptMs = 0;
static uint32_t s_lastLemonSparkAttemptMs = 0;
static uint32_t s_lastPairAuxAttemptMs = 0;
static uint32_t s_lastRenderedWsPriceMs = 0;
static uint32_t s_lastRotationMs = 0;
static uint32_t s_lastSparkSyncMs = 0;
static uint32_t s_lastBtcCardDrawMs = 0;
static constexpr uint32_t V2_BTC_CARD_REFRESH_MS = 1000;
static constexpr int64_t V2_INVALID_CARD_KEY =
    (-9223372036854775807LL - 1);
static int64_t s_lastPairDisplayKey = V2_INVALID_CARD_KEY;
static int64_t s_lastPairSparkKey = V2_INVALID_CARD_KEY;
static int s_lastBidInt = -1;
static int s_lastAskInt = -1;
static int s_lastStockPriceCents = -1;
static uint8_t s_lastFocusedStock = 255;
static bool s_newsFetchPending = false;
static float s_geckoPairPrice = 0.0f;
static uint32_t s_geckoPairLastUpdate = 0;
static ApiResult s_pairAuxResult = API_NETWORK_ERROR;
static bool s_bootComplete = false;
static OtaInfo s_otaInfo = {};
static uint32_t s_lastOtaCheckMs = 0;
static uint32_t s_lastOtaProbeMs = 0;
static bool s_otaCheckPending = false;
static uint32_t s_otaArmedUntilMs = 0;
static char s_pendingWifiSsid[33] = {};
static char s_pendingWifiPass[65] = {};
static bool s_hasPendingWifi = false;
static constexpr uint32_t V2_OTA_CHECK_MS = 5UL * 60UL * 1000UL;
static constexpr uint32_t V2_OTA_PROBE_MS = 60UL * 1000UL;

static void startNetworkServices();

static V2FetchStatus mapFetchStatus(ApiResult result) {
    switch (result) {
        case API_OK: return V2_FETCH_OK;
        case API_NETWORK_ERROR: return V2_FETCH_NETWORK_ERROR;
        case API_PARSE_ERROR: return V2_FETCH_PARSE_ERROR;
        case API_TIMEOUT: return V2_FETCH_TIMEOUT;
        case API_RATE_LIMITED: return V2_FETCH_RATE_LIMITED;
    }
    return V2_FETCH_NETWORK_ERROR;
}

static void setPairSource(const char* source) {
    strncpy(s_snapshot.pairSource, source, sizeof(s_snapshot.pairSource) - 1);
    s_snapshot.pairSource[sizeof(s_snapshot.pairSource) - 1] = '\0';
}

static bool pairUsesBinance(const PairDef& pair) {
    return pair.source != PAIR_GECKO_ONLY;
}

static int64_t pairDisplayKey(const V2RuntimeSnapshot& snapshot) {
    if (!snapshot.pairValid) return V2_INVALID_CARD_KEY;
    uint8_t pairIndex = snapshot.selectedPair < BTC_PAIR_COUNT
        ? snapshot.selectedPair : 0;
    const uint8_t decimals = BTC_PAIRS[pairIndex].decimals;
    const double scale = decimals == 0 ? 1.0
        : decimals == 1 ? 10.0 : 100.0;
    return static_cast<int64_t>(
        llround(static_cast<double>(snapshot.pairPrice) * scale));
}

static int64_t pairSparkKey(const V2RuntimeSnapshot& snapshot) {
    const SparklineData& spark = snapshot.pairSpark;
    if (!spark.valid || spark.count == 0) return V2_INVALID_CARD_KEY;
    return static_cast<int64_t>(llround(
        static_cast<double>(spark.points[spark.count - 1]) * 100.0));
}

static void refreshPairSnapshot(uint32_t nowMs, bool syncSpark = true) {
    uint8_t pairIndex = s_model.selectedPair < BTC_PAIR_COUNT ? s_model.selectedPair : 0;
    const PairDef& pair = BTC_PAIRS[pairIndex];
    bool wsValid = wsBinanceHasPrice();
    uint32_t wsUpdated = wsBinanceLastPriceMs();
    const bool pairChanged = s_snapshot.selectedPair != pairIndex;
    if (pairChanged) s_snapshot.pairSpark.valid = false;
    s_snapshot.selectedPair = pairIndex;
    s_snapshot.pairValid = false;
    s_snapshot.pairPrice = 0.0f;
    s_snapshot.pairLastUpdate = 0;

    if ((pair.source == PAIR_BINANCE_DIRECT || pair.source == PAIR_BINANCE_INVERT) && wsValid) {
        s_snapshot.pairPrice = wsBinanceGetPrice();
        s_snapshot.pairLastUpdate = wsUpdated;
        s_snapshot.pairValid = s_snapshot.pairPrice > 0;
        setPairSource(wsBinanceConnected() ? "BINANCE WS" : "BINANCE CACHE");
    } else if (pair.source == PAIR_DERIVED && wsValid && s_snapshot.lemon.valid) {
        float arsPerUsd = (s_snapshot.lemon.bid + s_snapshot.lemon.ask) * 0.5f;
        s_snapshot.pairPrice = wsBinanceGetPrice() * arsPerUsd;
        s_snapshot.pairLastUpdate = min(wsUpdated, static_cast<uint32_t>(s_snapshot.lemon.lastUpdate));
        s_snapshot.pairValid = s_snapshot.pairPrice > 0;
        setPairSource("BINANCE + CRIPTOYA");
    } else if (pair.source == PAIR_GECKO_ONLY && s_geckoPairPrice > 0) {
        s_snapshot.pairPrice = s_geckoPairPrice;
        s_snapshot.pairLastUpdate = s_geckoPairLastUpdate;
        s_snapshot.pairValid = true;
        setPairSource("COINGECKO");
    } else if (pairIndex == 0 && s_snapshot.btc.valid) {
        s_snapshot.pairPrice = s_snapshot.btc.usd;
        s_snapshot.pairLastUpdate = s_snapshot.btc.lastUpdate;
        s_snapshot.pairValid = true;
        setPairSource("COINGECKO FALLBACK");
    } else {
        setPairSource(pairUsesBinance(pair) ? "BINANCE" : "COINGECKO");
    }

    if (pairUsesBinance(pair) && wsValid) {
        bool refreshSpark = syncSpark &&
            (pairChanged || millis() - s_lastSparkSyncMs >= 1000 ||
             !s_snapshot.pairSpark.valid);
        if (refreshSpark) {
            s_lastSparkSyncMs = millis();
            wsBinanceGetSparkline(s_snapshot.pairSpark);
            if (pair.source == PAIR_DERIVED) {
                if (!s_snapshot.lemon.valid) {
                    s_snapshot.pairSpark.valid = false;
                } else {
                    float arsPerUsd = (s_snapshot.lemon.bid + s_snapshot.lemon.ask) * 0.5f;
                    s_snapshot.pairSpark.minVal = 1e30f;
                    s_snapshot.pairSpark.maxVal = -1e30f;
                    for (uint16_t i = 0; i < s_snapshot.pairSpark.count; i++) {
                        s_snapshot.pairSpark.points[i] *= arsPerUsd;
                        s_snapshot.pairSpark.minVal = min(s_snapshot.pairSpark.minVal, s_snapshot.pairSpark.points[i]);
                        s_snapshot.pairSpark.maxVal = max(s_snapshot.pairSpark.maxVal, s_snapshot.pairSpark.points[i]);
                    }
                }
            }
        }
    }

    uint32_t freshMs = pairUsesBinance(pair) ? 15000UL : UPDATE_BTC_PRICE_MS;
    uint32_t staleMs = pairUsesBinance(pair) ? 120000UL : UPDATE_BTC_PRICE_MS * 3UL;
    V2FetchStatus status = mapFetchStatus(s_pairAuxResult);
    if (pairUsesBinance(pair) && !wsValid) status = V2_FETCH_NETWORK_ERROR;
    s_snapshot.pairFreshness = v2Freshness(
        s_snapshot.pairValid, s_snapshot.pairLastUpdate, nowMs,
        s_snapshot.online, s_snapshot.pairFetching, status, freshMs, staleMs);
    if (pairUsesBinance(pair) && !wsBinanceConnected() &&
        s_snapshot.pairFreshness == V2_LIVE) {
        s_snapshot.pairFreshness = V2_CACHED;
    }
}

static void refreshSnapshot(bool syncSpark = true) {
    uint32_t nowMs = millis();
    s_snapshot.online = wifiConnected();
    s_snapshot.stocksFetching = stocksIsFetching();
    s_snapshot.stockCount = stocksGetWatchlistCount();
    if (s_snapshot.stockCount > V2_TAPE_ROWS) s_snapshot.stockCount = V2_TAPE_ROWS;
    s_snapshot.focusedStock = stocksGetFocusedIdx();
    for (uint8_t i = 0; i < s_snapshot.stockCount; i++) {
        stocksGetSnapshotAt(i, s_snapshot.stocks[i]);
    }
    s_snapshot.btcFreshness = v2Freshness(
        s_snapshot.btc.valid,
        s_snapshot.btc.lastUpdate,
        nowMs,
        s_snapshot.online,
        s_snapshot.btcFetching,
        mapFetchStatus(s_snapshot.btcResult),
        UPDATE_BTC_PRICE_MS,
        UPDATE_BTC_PRICE_MS * 3UL);
    s_snapshot.lemonFreshness = v2Freshness(
        s_snapshot.lemon.valid,
        s_snapshot.lemon.lastUpdate,
        nowMs,
        s_snapshot.online,
        s_snapshot.lemonFetching,
        mapFetchStatus(s_snapshot.lemonResult),
        UPDATE_LEMON_MS * 2UL,
        UPDATE_LEMON_MS * 10UL);
    refreshPairSnapshot(nowMs, syncSpark);
    const char* time = timeReady() ? getTimeStr(nvsGet24hFormat()) : "--:--:--";
    strncpy(s_snapshot.time, time, sizeof(s_snapshot.time) - 1);
    s_snapshot.time[sizeof(s_snapshot.time) - 1] = '\0';
    strncpy(s_snapshot.ssid, wifiConnected() ? wifiSSID() : "Sin conexion", sizeof(s_snapshot.ssid) - 1);
    s_snapshot.ssid[sizeof(s_snapshot.ssid) - 1] = '\0';
    String ip = wifiConnected() ? wifiIP() : String("--");
    strncpy(s_snapshot.ip, ip.c_str(), sizeof(s_snapshot.ip) - 1);
    s_snapshot.ip[sizeof(s_snapshot.ip) - 1] = '\0';
    s_snapshot.rssi = wifiConnected() ? wifiRSSI() : 0;
    s_snapshot.brightness = nvsGetBrightness();
    s_snapshot.rotationSeconds = nvsGetV2RotationSeconds();
    s_snapshot.use24h = nvsGet24hFormat();
    s_snapshot.soundEnabled = audioIsEnabled();
    s_snapshot.wifiResetArmed = s_model.wifiResetArmed;
    s_snapshot.otaArmed = s_otaArmedUntilMs != 0 && nowMs < s_otaArmedUntilMs;
    s_snapshot.uptimeSeconds = nowMs / 1000;
    s_snapshot.freeHeap = ESP.getFreeHeap();
}

static void drawNow() {
    refreshSnapshot();
    bool sceneChanged = s_drawnScene != static_cast<uint8_t>(s_model.scene);
    s_drawnScene = static_cast<uint8_t>(s_model.scene);
    v2UiDraw(s_snapshot, s_model, sceneChanged);
    s_dirty = false;
}

static void fetchBtcNow() {
    if (!wifiConnected() || stocksIsFetching()) return;
    s_snapshot.btcFetching = true;
    s_lastBtcAttemptMs = millis();
    s_snapshot.btcResult = fetchBtcPrice(s_snapshot.btc);
    s_snapshot.btcFetching = false;
}

static void fetchLemonNow() {
    if (!wifiConnected() || stocksIsFetching()) return;
    s_snapshot.lemonFetching = true;
    s_lastLemonAttemptMs = millis();
    s_snapshot.lemonResult = fetchLemonPrice(s_snapshot.lemon);
    s_snapshot.lemonFetching = false;
}

static void updateLemonChange24h() {
    const SparklineData& spark = s_snapshot.lemonSpark;
    s_snapshot.lemonChange24hValid = false;
    if (!spark.valid || spark.count < 2) return;
    const float first = spark.points[0];
    const float last = spark.points[spark.count - 1];
    if (!isfinite(first) || !isfinite(last) || first <= 0.0f) return;
    const float change = ((last - first) / first) * 100.0f;
    if (!isfinite(change)) return;
    s_snapshot.lemonChange24h = change;
    s_snapshot.lemonChange24hValid = true;
}

static void fetchLemonHistoryNow() {
    if (!wifiConnected() || stocksIsFetching()) return;
    s_lastLemonSparkAttemptMs = millis();
    fetchLemonSparkline(s_snapshot.lemonSpark, 1);
    updateLemonChange24h();
}

static void requestNewsFetch() {
    const char* sym = stocksGetFocusedSymbol();
    if (!sym || !sym[0]) {
        s_newsFetchPending = false;
        s_snapshot.newsFetching = false;
        s_snapshot.newsCount = 0;
        return;
    }

    uint8_t cachedCount = 0;
    newsGetCached(sym, s_snapshot.news, NEWS_MAX_ITEMS, cachedCount);
    if (cachedCount > 0) s_snapshot.newsCount = cachedCount;
    if (cachedCount == 0) s_snapshot.newsCount = 0;

    const bool cacheFresh = newsCacheIsFresh(sym);
    s_newsFetchPending = !cacheFresh;
    s_snapshot.newsFetching = !cacheFresh;
    if (s_model.scene == V2_HOME && s_bootComplete) {
        v2UiUpdateNews(s_snapshot, s_model);
    }
}

static void fetchNewsNow() {
    if (!wifiConnected() || stocksIsFetching()) return;
    const char* sym = stocksGetFocusedSymbol();
    if (!sym || !sym[0]) {
        s_newsFetchPending = false;
        s_snapshot.newsFetching = false;
        return;
    }
    s_newsFetchPending = false;
    apiStop();
    delay(100);
    uint8_t count = 0;
    newsFetch(sym, s_snapshot.news, NEWS_MAX_ITEMS, count);
    s_snapshot.newsCount = count;
    s_snapshot.newsFetching = false;
    if (s_model.scene == V2_HOME) {
        refreshSnapshot();
        v2UiUpdateNews(s_snapshot, s_model);
    }
}

static void fetchPairAuxNow() {
    if (!wifiConnected() || stocksIsFetching()) return;
    const PairDef& pair = BTC_PAIRS[s_model.selectedPair];
    s_snapshot.pairFetching = true;
    s_lastPairAuxAttemptMs = millis();
    if (pair.source == PAIR_DERIVED) {
        fetchLemonNow();
        s_pairAuxResult = s_snapshot.lemonResult;
    } else if (pair.source == PAIR_GECKO_ONLY) {
        s_pairAuxResult = fetchGeckoBtcPrice(pair.geckoVs, s_geckoPairPrice);
        if (s_pairAuxResult == API_OK) s_geckoPairLastUpdate = millis();
    } else {
        s_pairAuxResult = API_OK;
    }
    s_snapshot.pairFetching = false;
}

static void configurePairFeed() {
    if (!wifiConnected()) return;
    const PairDef& pair = BTC_PAIRS[s_model.selectedPair];
    s_snapshot.pairFetching = true;
    if (pair.source == PAIR_BINANCE_DIRECT || pair.source == PAIR_BINANCE_INVERT) {
        wsBinanceReconnect(pair.wsPath, pair.inverted);
        wsBinanceBackfillSymbol(pair.restSymbol, pair.inverted);
        s_pairAuxResult = API_OK;
    } else if (pair.source == PAIR_DERIVED) {
        const PairDef& usd = BTC_PAIRS[0];
        wsBinanceReconnect(usd.wsPath, usd.inverted);
        wsBinanceBackfillSymbol(usd.restSymbol, usd.inverted);
        s_pairAuxResult = s_snapshot.lemonResult;
    } else {
        wsBinanceStop();
        fetchPairAuxNow();
    }
    s_snapshot.pairFetching = false;
    s_lastRenderedWsPriceMs = wsBinanceLastPriceMs();
}

static void checkV2OtaNow(bool bootCheck) {
    if (!wifiConnected()) return;
    if (stocksIsFetching()) {
        s_otaCheckPending = true;
        s_snapshot.otaChecking = true;
        return;
    }
    s_otaCheckPending = false;
    s_snapshot.otaChecking = true;
    if (bootCheck) v2UiDrawLoading("BUSCANDO ACTUALIZACIONES", 38);

    if (!bootCheck) {
        scheduler.enable(taskStocks, false);
        stocksSetActive(false);
        wsBinanceStop();
    }
    s_otaInfo = otaCheckAsset(OTA_GITHUB_REPO, OTA_V2_ASSET, APP_VERSION);
    if (s_otaInfo.available && !s_otaInfo.md5[0]) {
        Serial.println("[V2 OTA] Ignoring update without channel-specific MD5");
        s_otaInfo.available = false;
        s_otaInfo.url[0] = '\0';
    }
    s_lastOtaCheckMs = millis();
    s_lastOtaProbeMs = s_lastOtaCheckMs;
    s_snapshot.otaChecked = true;
    s_snapshot.otaChecking = false;
    s_snapshot.otaAvailable = s_otaInfo.available;
    strncpy(s_snapshot.otaVersion, s_otaInfo.version, sizeof(s_snapshot.otaVersion) - 1);
    s_snapshot.otaVersion[sizeof(s_snapshot.otaVersion) - 1] = '\0';

    if (!bootCheck) {
        apiSetup();
        configurePairFeed();
        stocksSetActive(true);
        scheduler.enable(taskStocks, true);
    }
    refreshSnapshot();
    if (!bootCheck) {
        if (s_model.scene == V2_HOME) v2UiUpdateHeader(s_snapshot, s_model);
        else if (s_model.scene == V2_SETTINGS) s_dirty = true;
    }
}

static void installV2OtaNow() {
    if (!s_snapshot.otaAvailable || !s_snapshot.otaArmed || !s_otaInfo.url[0]) return;
    scheduler.enable(taskStocks, false);
    wsBinanceStop();
    apiStop();
    configServerStop();
    stocksStop();
    otaFlash(s_otaInfo.url, nullptr, s_otaInfo.md5[0] ? s_otaInfo.md5 : nullptr);

    // Success reboots. Rebuild all live services and UI only on failure.
    s_networkStarted = false;
    s_otaArmedUntilMs = 0;
    startNetworkServices();
    s_dirty = true;
}

static void selectNextPair() {
    s_model.selectedPair = v2NextPair(s_model.selectedPair, BTC_PAIR_COUNT);
    nvsSetV2Pair(s_model.selectedPair);
    s_geckoPairPrice = 0.0f;
    s_geckoPairLastUpdate = 0;
    s_pairAuxResult = API_NETWORK_ERROR;
    s_lastSparkSyncMs = 0;
    s_lastPairDisplayKey = V2_INVALID_CARD_KEY;
    s_lastPairSparkKey = V2_INVALID_CARD_KEY;
    if (s_networkStarted) configurePairFeed();
    refreshSnapshot();
    s_lastPairDisplayKey = pairDisplayKey(s_snapshot);
    s_lastPairSparkKey = pairSparkKey(s_snapshot);
    s_lastBtcCardDrawMs = millis();
    v2UiUpdateBtcCard(s_snapshot, s_model);
}

static void startNetworkServices() {
    if (!wifiConnected() || s_networkStarted) return;
    s_networkStarted = true;
    if (!s_bootComplete) v2UiDrawLoading("SINCRONIZANDO HORA", 30);
    timeSetup();
    if (!s_snapshot.otaChecked) checkV2OtaNow(true);
    apiSetup();
    configServerStart();
    if (!s_bootComplete) v2UiDrawLoading("CARGANDO BITCOIN", 48);
    fetchBtcNow();
    if (!s_bootComplete) v2UiDrawLoading("CARGANDO DOLAR LEMON", 62);
    fetchLemonNow();
    if (!s_bootComplete) v2UiDrawLoading("CARGANDO HISTORICO", 78);
    configurePairFeed();
    if (!s_bootComplete) v2UiDrawLoading("CARGANDO WATCHLIST", 90);
    stocksSetActive(true);
    scheduler.enable(taskStocks, true);
    stocksRequestBurst();
    requestNewsFetch();
    if (s_bootComplete) {
        refreshSnapshot();
        v2UiUpdateData(s_snapshot, s_model);
    }
}

static void startProvisioning() {
    s_provisioning = true;
    provisionStart();
    provisionDrawQR();
}

static void showWifiRecovery(const char* ssid) {
    s_model.scene = V2_WIFI_RECOVERY;
    s_model.sceneEnteredMs = millis();
    s_model.lastInteractionMs = millis();
    refreshSnapshot();
    strncpy(s_snapshot.ssid, ssid, sizeof(s_snapshot.ssid) - 1);
    s_snapshot.ssid[sizeof(s_snapshot.ssid) - 1] = '\0';
    v2UiDraw(s_snapshot, s_model);
    s_dirty = false;
}

static void rememberPendingWifi(const char* ssid, const char* pass) {
    strncpy(s_pendingWifiSsid, ssid, sizeof(s_pendingWifiSsid) - 1);
    s_pendingWifiSsid[sizeof(s_pendingWifiSsid) - 1] = '\0';
    strncpy(s_pendingWifiPass, pass, sizeof(s_pendingWifiPass) - 1);
    s_pendingWifiPass[sizeof(s_pendingWifiPass) - 1] = '\0';
    s_hasPendingWifi = true;
}

static void persistPendingWifiIfConnected() {
    if (!s_hasPendingWifi || !wifiConnected()) return;
    nvsSaveWifi(s_pendingWifiSsid, s_pendingWifiPass);
    memset(s_pendingWifiSsid, 0, sizeof(s_pendingWifiSsid));
    memset(s_pendingWifiPass, 0, sizeof(s_pendingWifiPass));
    s_hasPendingWifi = false;
}

static void finishConnectedBoot() {
    persistPendingWifiIfConnected();
    startNetworkServices();
    s_model.scene = V2_HOME;
    s_model.sceneEnteredMs = millis();
    s_model.lastInteractionMs = millis();
    v2UiDrawLoading("LISTO", 100);
    drawNow();
    s_bootComplete = true;
}

static void handleWifiRecoveryTouch() {
    TouchEvent event = touchLoop();
    if (event.gesture != TOUCH_TAP || event.x < 32 || event.x > 448 ||
        event.y < 320 || event.y > 410) return;
    v2UiDrawLoading("ABRIENDO CONFIGURACION", 15);
    startProvisioning();
    if (audioIsEnabled()) playTap();
}

static void handleSettingsAction(const TouchEvent& event) {
    if (s_model.scene != V2_SETTINGS || event.gesture == TOUCH_NONE) return;
    if (event.y < 126 || event.y >= 416) return;
    uint8_t row = static_cast<uint8_t>((event.y - 126) / 58);
    if (event.gesture == TOUCH_TAP && s_model.settingsPage == 0) {
        if (row == 0) {
            uint8_t current = nvsGetBrightness();
            uint8_t next = current < 96 ? 128 : current < 160 ? 192 : current < 224 ? 255 : 64;
            nvsSetBrightness(next);
            displaySetBrightness(next);
        } else if (row == 1) {
            nvsSet24hFormat(!nvsGet24hFormat());
        } else if (row == 2) {
            bool next = !audioIsEnabled();
            audioSetEnabled(next);
            nvsSetSoundEnabled(next);
        } else if (row == 3) {
            uint8_t current = nvsGetV2RotationSeconds();
            uint8_t next = current == 0 ? 15 : current == 15 ? 30 : current == 30 ? 60 : 0;
            nvsSetV2RotationSeconds(next);
        } else if (row == 4) {
            selectNextPair();
        }
        s_dirty = true;
    }
    if (s_model.settingsPage == 1 && row == 0 && event.gesture == TOUCH_TAP) {
        stocksAdvanceFocused();
        s_dirty = true;
    }
    if (s_model.settingsPage == 1 && row == 1 && event.gesture == TOUCH_LONG_PRESS) {
        uint32_t nowMs = millis();
        if (s_model.wifiResetArmed && nowMs < s_model.wifiResetUntilMs) {
            nvsForgetWifi();
            ESP.restart();
        }
        s_model.wifiResetArmed = true;
        s_model.wifiResetUntilMs = nowMs + 5000;
        s_dirty = true;
    }
    if (s_model.settingsPage == 2 && row == 2 &&
        event.gesture == TOUCH_TAP) {
        if (!s_snapshot.otaAvailable) {
            checkV2OtaNow(false);
            s_dirty = true;
        } else {
            if (s_snapshot.otaArmed) {
                installV2OtaNow();
            } else {
                s_otaArmedUntilMs = millis() + 8000;
                s_snapshot.otaArmed = true;
                s_dirty = true;
            }
        }
    }
}

static bool handleHomeUpdateTap(const TouchEvent& event) {
    if (s_model.scene != V2_HOME || event.gesture != TOUCH_TAP ||
        !s_snapshot.otaAvailable || event.x < 332 || event.x > 402 ||
        event.y < 18 || event.y > 62) return false;
    uint32_t nowMs = millis();
    s_model.scene = V2_SETTINGS;
    s_model.settingsPage = 2;
    s_model.sceneEnteredMs = nowMs;
    s_model.lastInteractionMs = nowMs;
    s_dirty = true;
    return true;
}

static void handleTouch() {
    TouchEvent event = touchLoop();
    if (event.gesture == TOUCH_NONE) return;
    if (handleHomeUpdateTap(event)) {
        if (audioIsEnabled()) playTap();
        return;
    }
    V2Scene before = s_model.scene;
    uint8_t pageBefore = s_model.settingsPage;
    if (event.gesture == TOUCH_TAP && s_model.scene == V2_SETTINGS) {
        int8_t tab = v2SettingsTabAt(event.x, event.y);
        if (tab >= 0) {
            s_model.settingsPage = static_cast<uint8_t>(tab);
            s_model.lastInteractionMs = millis();
            s_dirty = true;
            if (audioIsEnabled()) playTap();
            return;
        }
    }
    if (event.gesture == TOUCH_TAP && s_model.scene == V2_NEWS_READER) {
        if (event.y >= 120) {
            s_model.selectedNews = v2NextNews(
                s_model.selectedNews, s_snapshot.newsCount);
            s_model.lastInteractionMs = millis();
            s_dirty = true;
            if (audioIsEnabled()) playTap();
            return;
        }
    }
    if (event.gesture == TOUCH_TAP && s_model.scene == V2_HOME) {
        if (event.y >= 72 && event.y < 228) {
            stocksAdvanceFocused();
            s_lastFocusedStock = 255;
            s_lastStockPriceCents = -1;
            refreshSnapshot();
            v2UiUpdateStockHero(s_snapshot, s_model);
            requestNewsFetch();
            if (audioIsEnabled()) playTap();
            return;
        }
        if (event.y >= 328 && event.y < 448 && event.x >= 32 && event.x < 232) {
            selectNextPair();
            return;
        }
    }
    handleSettingsAction(event);
    v2HandleGesture(s_model, event.gesture, event.x, event.y, millis());
    if (before != s_model.scene || pageBefore != s_model.settingsPage) s_dirty = true;
    if (audioIsEnabled() && event.gesture == TOUCH_TAP) playTap();
}

void v2RuntimeSetup() {
    v2UiSetTheme(nvsGetV2Theme());
    v2UiSetup();
    v2UiDrawLoading("INICIANDO", 0);
    stocksInit();
    s_model.selectedPair = nvsGetV2Pair();
    taskStocks = scheduler.add("v2stocks", UPDATE_STOCKS_MS, stocksFetchTask);
    scheduler.enable(taskStocks, false);
    if (nvsHasWifi()) {
        char ssid[33] = {};
        char pass[65] = {};
        nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));
        v2UiDrawLoading("CONECTANDO WI-FI", 15);
        wifiSetup(ssid, pass);
        if (!wifiConnected()) {
            showWifiRecovery(ssid);
            return;
        }
        finishConnectedBoot();
        return;
    } else {
        v2UiDrawLoading("CONFIGURAR WI-FI", 15);
        startProvisioning();
        return;
    }
}

void v2RuntimeLoop() {
    if (s_provisioning) {
        if (provisionTick()) {
            char ssid[33] = {};
            char pass[65] = {};
            provisionGetCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
            provisionStop();
            s_provisioning = false;
            rememberPendingWifi(ssid, pass);
            v2UiDrawLoading("PROBANDO WI-FI", 20);
            wifiSetup(ssid, pass);
            if (wifiConnected()) {
                nvsSaveWifi(ssid, pass);
                s_hasPendingWifi = false;
                finishConnectedBoot();
            } else {
                showWifiRecovery(ssid);
            }
            return;
        }
        return;
    }

    if (s_model.scene == V2_WIFI_RECOVERY) {
        wifiLoop();
        if (wifiConnected()) {
            finishConnectedBoot();
        } else {
            handleWifiRecoveryTouch();
        }
        return;
    }

    wifiLoop();
    v2UiSetDeferred(true);
    bool online = wifiConnected();
    if (online && !s_networkStarted) startNetworkServices();
    if (!online && s_networkStarted) {
        s_networkStarted = false;
        wsBinanceStop();
        stocksSetActive(false);
        scheduler.enable(taskStocks, false);
        s_dirty = true;
    }

    if (s_networkStarted) wsBinanceLoop();

    scheduler.tick();
    if (stocksConsumeDirty()) {
        refreshSnapshot();
        if (s_model.scene == V2_HOME) v2UiUpdateStockPrice(s_snapshot, s_model);
        else v2UiUpdateData(s_snapshot, s_model);
    }
    if (s_newsFetchPending && online && !stocksIsFetching()) {
        fetchNewsNow();
    }

    uint32_t nowMs = millis();
    if (online && !stocksIsFetching() && !s_snapshot.newsFetching &&
        (s_lastLemonSparkAttemptMs == 0 ||
         nowMs - s_lastLemonSparkAttemptMs >= UPDATE_SPARKLINE_MS)) {
        fetchLemonHistoryNow();
        refreshSnapshot();
        if (s_model.scene == V2_HOME) {
            v2UiUpdateDollarCard(s_snapshot, s_model);
        } else {
            v2UiUpdateData(s_snapshot, s_model);
        }
    }
    if (online && s_otaCheckPending && !stocksIsFetching()) {
        checkV2OtaNow(false);
        nowMs = millis();
    }
    if (online && s_snapshot.otaChecked && !s_snapshot.otaChecking &&
        !s_snapshot.otaAvailable &&
        nowMs - s_lastOtaProbeMs >= V2_OTA_PROBE_MS &&
        !stocksIsFetching()) {
        s_lastOtaProbeMs = nowMs;
        if (otaLatestTagChanged(OTA_GITHUB_REPO, APP_VERSION)) {
            checkV2OtaNow(false);
            nowMs = millis();
        }
    }
    if (online && s_snapshot.otaChecked && !s_snapshot.otaChecking &&
        !s_snapshot.otaAvailable &&
        nowMs - s_lastOtaCheckMs >= V2_OTA_CHECK_MS && !stocksIsFetching()) {
        checkV2OtaNow(false);
    }
    uint32_t retryMs = s_snapshot.btc.valid ? UPDATE_BTC_PRICE_MS : 30000UL;
    if (online && nowMs - s_lastBtcAttemptMs >= retryMs) {
        fetchBtcNow();
        refreshSnapshot();
        if (s_model.scene == V2_HOME) {
            v2UiUpdatePriceOnly(s_snapshot, s_model);
        } else {
            v2UiUpdateData(s_snapshot, s_model);
        }
    }

    if (online && nowMs - s_lastLemonAttemptMs >= UPDATE_LEMON_MS) {
        fetchLemonNow();
        refreshSnapshot();
        if (s_model.scene == V2_HOME) {
            if (s_model.selectedPair == 3) {
                v2UiUpdatePriceOnly(s_snapshot, s_model);
            }
            int bidInt = s_snapshot.lemon.valid ? (int)s_snapshot.lemon.bid : 0;
            int askInt = s_snapshot.lemon.valid ? (int)s_snapshot.lemon.ask : 0;
            if (bidInt != s_lastBidInt || askInt != s_lastAskInt) {
                s_lastBidInt = bidInt;
                s_lastAskInt = askInt;
                v2UiUpdateDollarCard(s_snapshot, s_model);
            }
        } else {
            v2UiUpdateData(s_snapshot, s_model);
        }
    }

    const PairDef& pair = BTC_PAIRS[s_model.selectedPair];
    uint32_t auxRefreshMs = UPDATE_BTC_PRICE_MS;
    if (online && pair.source == PAIR_GECKO_ONLY &&
        nowMs - s_lastPairAuxAttemptMs >= auxRefreshMs) {
        fetchPairAuxNow();
        refreshSnapshot();
        v2UiUpdateBtcCard(s_snapshot, s_model);
    }

    uint32_t wsPriceMs = wsBinanceLastPriceMs();
    if (pairUsesBinance(pair) && wsPriceMs != 0 && wsPriceMs != s_lastRenderedWsPriceMs) {
        s_lastRenderedWsPriceMs = wsPriceMs;
        const uint32_t sparkSyncBefore = s_lastSparkSyncMs;
        refreshSnapshot(true);
        const int64_t displayKey = pairDisplayKey(s_snapshot);
        const int64_t sparkKey = pairSparkKey(s_snapshot);
        const bool displayChanged = displayKey != s_lastPairDisplayKey;
        const bool sparkChanged =
            s_lastSparkSyncMs != sparkSyncBefore &&
            sparkKey != s_lastPairSparkKey;
        if ((displayChanged || sparkChanged) &&
            nowMs - s_lastBtcCardDrawMs >= V2_BTC_CARD_REFRESH_MS) {
            s_lastPairDisplayKey = displayKey;
            s_lastPairSparkKey = sparkKey;
            s_lastBtcCardDrawMs = nowMs;
            v2UiUpdateBtcCard(s_snapshot, s_model);
        }
    }

    uint8_t rotation = nvsGetV2RotationSeconds();
    if (rotation > 0 && s_model.scene == V2_HOME &&
        nowMs - s_lastRotationMs >= static_cast<uint32_t>(rotation) * 1000UL) {
        s_lastRotationMs = nowMs;
        stocksAdvanceFocused();
        refreshSnapshot();
        s_lastFocusedStock = 255;
        v2UiUpdateStockHero(s_snapshot, s_model);
        requestNewsFetch();
    }

    if (v2ApplyTimeout(s_model, nowMs)) s_dirty = true;
    if (s_model.wifiResetArmed && nowMs >= s_model.wifiResetUntilMs) {
        s_model.wifiResetArmed = false;
        s_dirty = true;
    }
    if (s_otaArmedUntilMs != 0 && nowMs >= s_otaArmedUntilMs) {
        s_otaArmedUntilMs = 0;
        s_snapshot.otaArmed = false;
        if (s_model.scene == V2_HOME || s_model.scene == V2_SETTINGS) s_dirty = true;
    }
    handleTouch();
    if (nowMs - s_lastClockDrawMs >= 1000) {
        s_lastClockDrawMs = nowMs;
        const char* time = timeReady() ? getTimeStr(nvsGet24hFormat()) : "--:--:--";
        strncpy(s_snapshot.time, time, sizeof(s_snapshot.time) - 1);
        s_snapshot.time[sizeof(s_snapshot.time) - 1] = '\0';
        v2UiUpdateClock(s_snapshot.time, s_model.scene);
    }
    if (s_dirty) drawNow();
    else v2UiFlushDeferred();
    esp_task_wdt_reset();
}

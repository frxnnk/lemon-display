#include "nvs_storage.h"
#include "data_models.h"
#include "config.h"
#include <Preferences.h>
#include <Arduino.h>
#include <esp_system.h>
#include <cstring>

static Preferences prefs;
static const char* NVS_NAMESPACE = "lemon";

// XOR-obfuscate a buffer with the device's unique eFuse MAC (per-device key)
static void xorWithMac(uint8_t* buf, size_t len) {
    uint64_t mac = ESP.getEfuseMac();
    uint8_t key[8];
    memcpy(key, &mac, 8);
    for (size_t i = 0; i < len; i++) {
        buf[i] ^= key[i % 8];
    }
}

void nvsInit() {
    prefs.begin(NVS_NAMESPACE, false);
    Serial.println("[NVS] Initialized");
}

// ── WiFi ──

bool nvsHasWifi() {
    return prefs.isKey("wifi_ssid") && prefs.getString("wifi_ssid", "").length() > 0;
}

void nvsLoadWifi(char* ssid, size_t ssidLen, char* pass, size_t passLen) {
    String s = prefs.getString("wifi_ssid", "");
    strncpy(ssid, s.c_str(), ssidLen - 1);
    ssid[ssidLen - 1] = '\0';

    // Try new XOR-obfuscated format first
    uint8_t wpLen = prefs.getUChar("wp_len", 0);
    if (wpLen > 0 && wpLen < passLen) {
        uint8_t enc[65];
        size_t rd = prefs.getBytes("wifi_pe", enc, wpLen);
        if (rd == wpLen) {
            xorWithMac(enc, wpLen);
            memcpy(pass, enc, wpLen);
            pass[wpLen] = '\0';
            return;
        }
    }

    // Fallback: read legacy plaintext and auto-migrate
    String p = prefs.getString("wifi_pass", "");
    strncpy(pass, p.c_str(), passLen - 1);
    pass[passLen - 1] = '\0';

    // Auto-migrate to obfuscated format
    if (p.length() > 0) {
        nvsSaveWifi(ssid, pass);
    }
}

void nvsSaveWifi(const char* ssid, const char* pass) {
    prefs.putString("wifi_ssid", ssid);

    // Store password XOR-obfuscated (not plaintext)
    uint8_t len = (uint8_t)strlen(pass);
    uint8_t enc[65];
    memcpy(enc, pass, len);
    xorWithMac(enc, len);
    prefs.putBytes("wifi_pe", enc, len);
    prefs.putUChar("wp_len", len);

    // Remove legacy plaintext key
    prefs.remove("wifi_pass");

    Serial.printf("[NVS] WiFi saved: %s\n", ssid);
}

void nvsForgetWifi() {
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pass");
    prefs.remove("wifi_pe");
    prefs.remove("wp_len");
    Serial.println("[NVS] WiFi credentials erased");
}

// ── Display ──

uint8_t nvsGetBrightness() {
    return prefs.getUChar("brightness", 255);
}

void nvsSetBrightness(uint8_t val) {
    prefs.putUChar("brightness", val);
}

uint8_t nvsGetTheme() {
    uint8_t theme = prefs.getUChar("ui_theme", 0);
    return (theme > 1) ? 0 : theme;
}

void nvsSetTheme(uint8_t theme) {
    if (theme > 1) theme = 0;
    prefs.putUChar("ui_theme", theme);
}

// ── Sound ──

bool nvsGetSoundEnabled() {
    return prefs.getUChar("sound_on", 1) != 0;
}

void nvsSetSoundEnabled(bool on) {
    prefs.putUChar("sound_on", on ? 1 : 0);
}

uint8_t nvsGetUsdtLanguage() {
    const uint8_t language = prefs.getUChar("usdt_lang", 0);
    return language > 1 ? 0 : language;
}

void nvsSetUsdtLanguage(uint8_t language) {
    prefs.putUChar("usdt_lang", language > 1 ? 0 : language);
}

// ── Alerts ──

bool nvsGetAlertEnabled() {
    return prefs.getUChar("alert_on", 1) != 0;
}

void nvsSetAlertEnabled(bool on) {
    prefs.putUChar("alert_on", on ? 1 : 0);
}

// ── Time Format ──

bool nvsGet24hFormat() {
    return prefs.getUChar("fmt_24h", 1) != 0;
}

void nvsSet24hFormat(bool on) {
    prefs.putUChar("fmt_24h", on ? 1 : 0);
}

// ── Dashboard Layout ──

uint8_t nvsGetLayout() {
    uint8_t v = prefs.getUChar("layout", 1);  // Default: BTC + USD
    return (v > 1) ? 1 : v;  // Clamp legacy layout values
}

void nvsSetLayout(uint8_t idx) {
    if (idx > 1) idx = 0;
    prefs.putUChar("layout", idx);
}

bool nvsGetProMode() {
    return prefs.getUChar("pro_mode", 0) != 0;
}

void nvsSetProMode(bool on) {
    prefs.putUChar("pro_mode", on ? 1 : 0);
}

// ── Tutorial ──

bool nvsGetTutorialDone() {
    return prefs.getUChar("tut_done", 0) != 0;
}

void nvsSetTutorialDone(bool done) {
    prefs.putUChar("tut_done", done ? 1 : 0);
}

bool nvsGetProTutDone() {
    return prefs.getUChar("pro_tut", 0) != 0;
}

void nvsSetProTutDone(bool done) {
    prefs.putUChar("pro_tut", done ? 1 : 0);
}

// ── Polymarket Predictions ──

#include "data_models.h"

static void polyStatsKeys(uint8_t periodIdx, char* kWins, size_t kWinsLen,
                          char* kLosses, size_t kLossesLen,
                          char* kStreak, size_t kStreakLen,
                          char* kBest, size_t kBestLen) {
    snprintf(kWins,   kWinsLen,   "pw%u", (unsigned)periodIdx);
    snprintf(kLosses, kLossesLen, "pl%u", (unsigned)periodIdx);
    snprintf(kStreak, kStreakLen, "ps%u", (unsigned)periodIdx);
    snprintf(kBest,   kBestLen,   "pb%u", (unsigned)periodIdx);
}

void nvsLoadPolyStats(uint8_t periodIdx, PolyStats& stats) {
    char kWins[8], kLosses[8], kStreak[8], kBest[8];
    polyStatsKeys(periodIdx, kWins, sizeof(kWins), kLosses, sizeof(kLosses),
                  kStreak, sizeof(kStreak), kBest, sizeof(kBest));

    stats.wins      = prefs.getUShort(kWins, 0);
    stats.losses    = prefs.getUShort(kLosses, 0);
    stats.pending   = 0;
    stats.streak    = prefs.getUShort(kStreak, 0);
    stats.bestStreak = prefs.getUShort(kBest, 0);
}

void nvsSavePolyStats(uint8_t periodIdx, const PolyStats& stats) {
    char kWins[8], kLosses[8], kStreak[8], kBest[8];
    polyStatsKeys(periodIdx, kWins, sizeof(kWins), kLosses, sizeof(kLosses),
                  kStreak, sizeof(kStreak), kBest, sizeof(kBest));

    prefs.putUShort(kWins, stats.wins);
    prefs.putUShort(kLosses, stats.losses);
    prefs.putUShort(kStreak, stats.streak);
    prefs.putUShort(kBest, stats.bestStreak);
}

static void polyPredKeys(uint8_t periodIdx,
                         char* kActive, size_t kActiveLen,
                         char* kCond, size_t kCondLen,
                         char* kYes, size_t kYesLen,
                         char* kProb, size_t kProbLen,
                         char* kRef, size_t kRefLen,
                         char* kEnd, size_t kEndLen,
                         char* kTs, size_t kTsLen,
                         char* kPidx, size_t kPidxLen) {
    snprintf(kActive, kActiveLen, "pa%u", (unsigned)periodIdx);
    snprintf(kCond,   kCondLen,   "pc%u", (unsigned)periodIdx);
    snprintf(kYes,    kYesLen,    "py%u", (unsigned)periodIdx);
    snprintf(kProb,   kProbLen,   "pp%u", (unsigned)periodIdx);
    snprintf(kRef,    kRefLen,    "pr%u", (unsigned)periodIdx);
    snprintf(kEnd,    kEndLen,    "pe%u", (unsigned)periodIdx);
    snprintf(kTs,     kTsLen,     "pt%u", (unsigned)periodIdx);
    snprintf(kPidx,   kPidxLen,   "pi%u", (unsigned)periodIdx);
}

static void migrateLegacyPolyPrediction() {
    if (prefs.getUChar("pm_active", 0) == 0) return;

    uint8_t periodIdx = prefs.getUChar("pm_pidx", 255);
    if (periodIdx < BTC_PERIOD_COUNT) {
        char kActive[8], kCond[8], kYes[8], kProb[8], kRef[8], kEnd[8], kTs[8], kPidx[8];
        polyPredKeys(periodIdx, kActive, sizeof(kActive), kCond, sizeof(kCond),
                     kYes, sizeof(kYes), kProb, sizeof(kProb), kRef, sizeof(kRef),
                     kEnd, sizeof(kEnd), kTs, sizeof(kTs), kPidx, sizeof(kPidx));
        if (prefs.getUChar(kActive, 0) == 0) {
            prefs.putUChar(kActive, 1);
            prefs.putString(kCond, prefs.getString("pm_cond", ""));
            prefs.putUChar(kYes, prefs.getUChar("pm_yes", 1));
            prefs.putFloat(kProb, prefs.getFloat("pm_prob", 0.5f));
            prefs.putFloat(kRef, prefs.getFloat("pm_ref", 0.0f));
            prefs.putULong(kEnd, prefs.getULong("pm_end", 0));
            prefs.putULong(kTs, prefs.getULong("pm_ts", 0));
            prefs.putUChar(kPidx, periodIdx);
        }
    }

    prefs.remove("pm_active");
    prefs.remove("pm_cond");
    prefs.remove("pm_yes");
    prefs.remove("pm_prob");
    prefs.remove("pm_ref");
    prefs.remove("pm_end");
    prefs.remove("pm_ts");
    prefs.remove("pm_pidx");
}

bool nvsHasPolyPrediction(uint8_t periodIdx) {
    migrateLegacyPolyPrediction();
    if (periodIdx >= BTC_PERIOD_COUNT) return false;

    char kActive[8], kCond[8], kYes[8], kProb[8], kRef[8], kEnd[8], kTs[8], kPidx[8];
    polyPredKeys(periodIdx, kActive, sizeof(kActive), kCond, sizeof(kCond),
                 kYes, sizeof(kYes), kProb, sizeof(kProb), kRef, sizeof(kRef),
                 kEnd, sizeof(kEnd), kTs, sizeof(kTs), kPidx, sizeof(kPidx));
    return prefs.getUChar(kActive, 0) != 0;
}

void nvsLoadPolyPrediction(uint8_t periodIdx, PolyPrediction& pred) {
    memset(&pred, 0, sizeof(pred));
    if (!nvsHasPolyPrediction(periodIdx)) return;

    char kActive[8], kCond[8], kYes[8], kProb[8], kRef[8], kEnd[8], kTs[8], kPidx[8];
    polyPredKeys(periodIdx, kActive, sizeof(kActive), kCond, sizeof(kCond),
                 kYes, sizeof(kYes), kProb, sizeof(kProb), kRef, sizeof(kRef),
                 kEnd, sizeof(kEnd), kTs, sizeof(kTs), kPidx, sizeof(kPidx));

    String condId = prefs.getString(kCond, "");
    strncpy(pred.conditionId, condId.c_str(), PM_COND_ID_LEN - 1);
    pred.conditionId[PM_COND_ID_LEN - 1] = '\0';
    pred.chosenYes = prefs.getUChar(kYes, 1) != 0;
    pred.probAtBet = prefs.getFloat(kProb, 0.5f);
    pred.refPrice  = prefs.getFloat(kRef, 0.0f);
    pred.endEpoch  = prefs.getULong(kEnd, 0);
    pred.timestamp = prefs.getULong(kTs, 0);
    pred.periodIdx = prefs.getUChar(kPidx, periodIdx);
    pred.resolved  = 0;  // Active = pending
}

void nvsSavePolyPrediction(uint8_t periodIdx, const PolyPrediction& pred) {
    if (periodIdx >= BTC_PERIOD_COUNT) return;

    char kActive[8], kCond[8], kYes[8], kProb[8], kRef[8], kEnd[8], kTs[8], kPidx[8];
    polyPredKeys(periodIdx, kActive, sizeof(kActive), kCond, sizeof(kCond),
                 kYes, sizeof(kYes), kProb, sizeof(kProb), kRef, sizeof(kRef),
                 kEnd, sizeof(kEnd), kTs, sizeof(kTs), kPidx, sizeof(kPidx));

    prefs.putUChar(kActive, 1);
    prefs.putString(kCond, pred.conditionId);
    prefs.putUChar(kYes, pred.chosenYes ? 1 : 0);
    prefs.putFloat(kProb, pred.probAtBet);
    prefs.putFloat(kRef, pred.refPrice);
    prefs.putULong(kEnd, pred.endEpoch);
    prefs.putULong(kTs, pred.timestamp);
    prefs.putUChar(kPidx, pred.periodIdx);
    Serial.printf("[NVS] Poly prediction saved: %s @ %.0f%%\n",
                  pred.chosenYes ? "YES" : "NO", pred.probAtBet * 100);
}

void nvsClearPolyPrediction(uint8_t periodIdx) {
    if (periodIdx >= BTC_PERIOD_COUNT) return;

    char kActive[8], kCond[8], kYes[8], kProb[8], kRef[8], kEnd[8], kTs[8], kPidx[8];
    polyPredKeys(periodIdx, kActive, sizeof(kActive), kCond, sizeof(kCond),
                 kYes, sizeof(kYes), kProb, sizeof(kProb), kRef, sizeof(kRef),
                 kEnd, sizeof(kEnd), kTs, sizeof(kTs), kPidx, sizeof(kPidx));

    prefs.remove(kActive);
    prefs.remove(kCond);
    prefs.remove(kYes);
    prefs.remove(kProb);
    prefs.remove(kRef);
    prefs.remove(kEnd);
    prefs.remove(kTs);
    prefs.remove(kPidx);
    Serial.println("[NVS] Poly prediction cleared");
}

// ── Prediction History ──

void nvsSavePredHistory(const PredHistoryEntry* entries, uint8_t head, uint8_t count) {
    prefs.putUChar("ph_head", head);
    prefs.putUChar("ph_cnt", count);
    // 10 bytes per entry: 4(timestamp) + 1(periodIdx) + 1(chosenYes) + 4(probAtBet) + 1(result) = 11
    // Use putBytes with raw struct array (compact enough for NVS)
    size_t sz = sizeof(PredHistoryEntry) * PRED_HISTORY_MAX;
    prefs.putBytes("ph_data", entries, sz);
}

void nvsLoadPredHistory(PredHistoryEntry* entries, uint8_t& head, uint8_t& count) {
    head = prefs.getUChar("ph_head", 0);
    count = prefs.getUChar("ph_cnt", 0);
    if (count > PRED_HISTORY_MAX) count = PRED_HISTORY_MAX;
    if (head >= PRED_HISTORY_MAX) head = 0;
    size_t sz = sizeof(PredHistoryEntry) * PRED_HISTORY_MAX;
    size_t read = prefs.getBytes("ph_data", entries, sz);
    if (read != sz) {
        memset(entries, 0, sz);
        head = 0;
        count = 0;
    }
}

// ── Stocks watchlist ──
// Stored as single comma-separated string under "wl". Keeps NVS simple and
// roundtrip-safe with the captive portal (which exchanges JSON).

static void loadDefaultWatchlist(StockWatchlist& out) {
    out.count = 0;
    uint8_t n = STOCK_DEFAULT_WATCHLIST_COUNT;
    if (n > STOCK_MAX_SYMBOLS) n = STOCK_MAX_SYMBOLS;
    for (uint8_t i = 0; i < n; i++) {
        strncpy(out.symbols[i], STOCK_DEFAULT_WATCHLIST[i], STOCK_SYMBOL_LEN - 1);
        out.symbols[i][STOCK_SYMBOL_LEN - 1] = '\0';
        out.count++;
    }
}

void nvsLoadWatchlist(StockWatchlist& out) {
    out.count = 0;
    String csv = prefs.getString("wl", "");
    if (csv.length() == 0) {
        loadDefaultWatchlist(out);
        return;
    }
    int start = 0;
    while (start < (int)csv.length() && out.count < STOCK_MAX_SYMBOLS) {
        int comma = csv.indexOf(',', start);
        int end = (comma < 0) ? (int)csv.length() : comma;
        int len = end - start;
        if (len > 0 && len < STOCK_SYMBOL_LEN) {
            memcpy(out.symbols[out.count], csv.c_str() + start, len);
            out.symbols[out.count][len] = '\0';
            // Uppercase + strip whitespace
            char* s = out.symbols[out.count];
            int w = 0;
            for (int r = 0; s[r]; r++) {
                char c = s[r];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
                if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                s[w++] = c;
            }
            s[w] = '\0';
            if (w > 0) out.count++;
        }
        if (comma < 0) break;
        start = comma + 1;
    }
    if (out.count == 0) loadDefaultWatchlist(out);
}

void nvsSaveWatchlist(const StockWatchlist& wl) {
    String csv;
    for (uint8_t i = 0; i < wl.count && i < STOCK_MAX_SYMBOLS; i++) {
        if (i > 0) csv += ',';
        csv += wl.symbols[i];
    }
    prefs.putString("wl", csv);
    Serial.printf("[NVS] Watchlist saved (%u): %s\n", (unsigned)wl.count, csv.c_str());
}

// ── Stocks quote cache ──
// Blob of up to STOCK_MAX_SYMBOLS quotes. Keyed by "sq_data" + "sq_cnt".
// Version byte ("sq_ver") guards against struct layout changes.
static const uint8_t STOCK_QUOTES_VER = 1;

void nvsLoadStockQuotes(StockQuote* out, uint8_t& count) {
    count = 0;
    uint8_t ver = prefs.getUChar("sq_ver", 0);
    if (ver != STOCK_QUOTES_VER) {
        memset(out, 0, sizeof(StockQuote) * STOCK_MAX_SYMBOLS);
        return;
    }
    uint8_t cnt = prefs.getUChar("sq_cnt", 0);
    if (cnt > STOCK_MAX_SYMBOLS) cnt = STOCK_MAX_SYMBOLS;
    size_t sz = sizeof(StockQuote) * STOCK_MAX_SYMBOLS;
    size_t read = prefs.getBytes("sq_data", out, sz);
    if (read != sz) {
        memset(out, 0, sz);
        return;
    }
    count = cnt;
    // Fetch timestamps are millis() — meaningless across reboots. Zero them
    // so the UI renders the data as "stale" (very old) until a fresh fetch.
    for (uint8_t i = 0; i < count; i++) out[i].lastUpdate = 0;
}

void nvsSaveStockQuotes(const StockQuote* quotes, uint8_t count) {
    if (count > STOCK_MAX_SYMBOLS) count = STOCK_MAX_SYMBOLS;
    prefs.putUChar("sq_ver", STOCK_QUOTES_VER);
    prefs.putUChar("sq_cnt", count);
    size_t sz = sizeof(StockQuote) * STOCK_MAX_SYMBOLS;
    prefs.putBytes("sq_data", quotes, sz);
}

// ── Stocks sparkline cache ──
static const uint8_t STOCK_SPARKS_VER = 2;
static const uint16_t STOCK_SPARK_CACHE_POINTS = 96;

struct StockSparkCacheEntry {
    char symbol[STOCK_SYMBOL_LEN];
    uint16_t count;
    float minVal;
    float maxVal;
    bool valid;
    float points[STOCK_SPARK_CACHE_POINTS];
};

static const size_t SP_DATA_BYTES = sizeof(StockSparkCacheEntry) * STOCK_MAX_SYMBOLS;

static void copySparkToCache(const SparklineData& src, const char* symbol, StockSparkCacheEntry& dst) {
    memset(&dst, 0, sizeof(dst));
    if (!src.valid || src.count < 2 || !symbol || symbol[0] == '\0') return;

    strncpy(dst.symbol, symbol, STOCK_SYMBOL_LEN - 1);
    dst.symbol[STOCK_SYMBOL_LEN - 1] = '\0';
    dst.valid = true;
    dst.count = (src.count < STOCK_SPARK_CACHE_POINTS) ? src.count : STOCK_SPARK_CACHE_POINTS;
    dst.minVal = 1e12f;
    dst.maxVal = -1e12f;

    for (uint16_t i = 0; i < dst.count; i++) {
        uint16_t srcIdx = (dst.count <= 1)
            ? 0
            : (uint16_t)(((uint32_t)i * (uint32_t)(src.count - 1)) / (uint32_t)(dst.count - 1));
        float v = src.points[srcIdx];
        dst.points[i] = v;
        if (v < dst.minVal) dst.minVal = v;
        if (v > dst.maxVal) dst.maxVal = v;
    }

    if (dst.maxVal <= dst.minVal) {
        dst.minVal = src.minVal;
        dst.maxVal = src.maxVal;
    }
}

static void copyCacheToSpark(const StockSparkCacheEntry& src, SparklineData& dst) {
    memset(&dst, 0, sizeof(dst));
    if (!src.valid || src.count < 2) return;

    dst.count = (src.count < SPARKLINE_POINTS) ? src.count : SPARKLINE_POINTS;
    for (uint16_t i = 0; i < dst.count; i++) dst.points[i] = src.points[i];
    dst.minVal = src.minVal;
    dst.maxVal = src.maxVal;
    dst.valid = true;
    dst.lastUpdate = 0;
}

void nvsLoadStockSparks(SparklineData* out, const StockWatchlist& wl) {
    for (uint8_t i = 0; i < STOCK_MAX_SYMBOLS; i++) out[i].valid = false;

    uint8_t ver = prefs.getUChar("sp_ver", 0);
    if (ver != STOCK_SPARKS_VER) return;

    StockSparkCacheEntry* cache = (StockSparkCacheEntry*)ps_malloc(SP_DATA_BYTES);
    if (!cache) {
        Serial.println("[NVS] sparks load: ps_malloc failed");
        return;
    }

    if (prefs.getBytes("sp_data", cache, SP_DATA_BYTES) != SP_DATA_BYTES) {
        free(cache);
        return;
    }

    uint8_t placed = 0;
    for (uint8_t s = 0; s < STOCK_MAX_SYMBOLS; s++) {
        if (!cache[s].valid || cache[s].symbol[0] == '\0') continue;
        for (uint8_t w = 0; w < wl.count; w++) {
            if (strncmp(cache[s].symbol, wl.symbols[w], STOCK_SYMBOL_LEN) == 0) {
                copyCacheToSpark(cache[s], out[w]);
                placed++;
                break;
            }
        }
    }
    free(cache);
    Serial.printf("[NVS] Stock sparks loaded: %u/%u\n",
                  (unsigned)placed, (unsigned)wl.count);
}

void nvsSaveStockSparks(const SparklineData* sparks, const StockWatchlist& wl) {
    StockSparkCacheEntry* cache = (StockSparkCacheEntry*)ps_malloc(SP_DATA_BYTES);
    if (!cache) {
        Serial.println("[NVS] sparks save: ps_malloc failed, skipping");
        return;
    }
    memset(cache, 0, SP_DATA_BYTES);

    for (uint8_t i = 0; i < wl.count && i < STOCK_MAX_SYMBOLS; i++) {
        copySparkToCache(sparks[i], wl.symbols[i], cache[i]);
    }

    if (prefs.isKey("sp_syms")) prefs.remove("sp_syms");
    prefs.remove("sp_data");
    prefs.putUChar("sp_ver", STOCK_SPARKS_VER);
    size_t written = prefs.putBytes("sp_data", cache, SP_DATA_BYTES);
    if (written != SP_DATA_BYTES) {
        Serial.printf("[NVS] Stock sparks cache write failed: %u/%u bytes\n",
                      (unsigned)written, (unsigned)SP_DATA_BYTES);
    }
    free(cache);
}

// Local Studio pairing token

bool nvsGetPairingToken(char* out, size_t outLen) {
    if (!out || outLen == 0) return false;
    String token = prefs.getString("pair_tok", "");
    if (token.length() == 0) {
        out[0] = '\0';
        return false;
    }
    strncpy(out, token.c_str(), outLen - 1);
    out[outLen - 1] = '\0';
    return true;
}

void nvsSetPairingToken(const char* token) {
    if (!token || token[0] == '\0') return;
    prefs.putString("pair_tok", token);
}

void nvsClearPairingToken() {
    prefs.remove("pair_tok");
}

// ── Z2 slot mode ──

uint8_t nvsGetZ2Mode() {
    uint8_t m = prefs.getUChar("z2_mode", 0);
    return (m < 3) ? m : 0;
}

void nvsSetZ2Mode(uint8_t mode) {
    if (mode > 2) mode = 0;
    prefs.putUChar("z2_mode", mode);
}

// ── Factory Reset ──

uint8_t nvsGetV2RotationSeconds() {
    uint8_t seconds = prefs.getUChar("v2_rot", 15);
    if (seconds == 0 || seconds == 15 || seconds == 30 || seconds == 60) return seconds;
    return 15;
}

void nvsSetV2RotationSeconds(uint8_t seconds) {
    if (seconds != 0 && seconds != 15 && seconds != 30 && seconds != 60) seconds = 15;
    prefs.putUChar("v2_rot", seconds);
}

uint8_t nvsGetV2Pair() {
    uint8_t pair = prefs.getUChar("v2_pair", 0);
    return pair < BTC_PAIR_COUNT ? pair : 0;
}

void nvsSetV2Pair(uint8_t pair) {
    prefs.putUChar("v2_pair", pair < BTC_PAIR_COUNT ? pair : 0);
}

uint8_t nvsGetV2Theme() {
    return prefs.getUChar("v2_theme", 1);
}

void nvsSetV2Theme(uint8_t theme) {
    prefs.putUChar("v2_theme", theme);
}

void nvsFactoryReset() {
    prefs.clear();
    Serial.println("[NVS] Factory reset — all keys erased");
}

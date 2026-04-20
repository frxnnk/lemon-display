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

// ── Sound ──

bool nvsGetSoundEnabled() {
    return prefs.getUChar("sound_on", 1) != 0;
}

void nvsSetSoundEnabled(bool on) {
    prefs.putUChar("sound_on", on ? 1 : 0);
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

bool nvsHasPolyPrediction() {
    return prefs.getUChar("pm_active", 0) != 0;
}

void nvsLoadPolyPrediction(PolyPrediction& pred) {
    memset(&pred, 0, sizeof(pred));
    if (!nvsHasPolyPrediction()) return;

    String condId = prefs.getString("pm_cond", "");
    strncpy(pred.conditionId, condId.c_str(), PM_COND_ID_LEN - 1);
    pred.conditionId[PM_COND_ID_LEN - 1] = '\0';
    pred.chosenYes = prefs.getUChar("pm_yes", 1) != 0;
    pred.probAtBet = prefs.getFloat("pm_prob", 0.5f);
    pred.timestamp = prefs.getULong("pm_ts", 0);
    pred.periodIdx = prefs.getUChar("pm_pidx", 255);
    pred.resolved  = 0;  // Active = pending
}

void nvsSavePolyPrediction(const PolyPrediction& pred) {
    prefs.putUChar("pm_active", 1);
    prefs.putString("pm_cond", pred.conditionId);
    prefs.putUChar("pm_yes", pred.chosenYes ? 1 : 0);
    prefs.putFloat("pm_prob", pred.probAtBet);
    prefs.putULong("pm_ts", pred.timestamp);
    prefs.putUChar("pm_pidx", pred.periodIdx);
    Serial.printf("[NVS] Poly prediction saved: %s @ %.0f%%\n",
                  pred.chosenYes ? "YES" : "NO", pred.probAtBet * 100);
}

void nvsClearPolyPrediction() {
    prefs.putUChar("pm_active", 0);
    prefs.remove("pm_cond");
    prefs.remove("pm_yes");
    prefs.remove("pm_prob");
    prefs.remove("pm_ts");
    prefs.remove("pm_pidx");
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
                if (c == ' ' || c == '\t') continue;
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
// Saved as two parallel blobs: "sp_syms" (symbol strings) + "sp_data"
// (SparklineData array). Symbol keys make the cache robust to watchlist
// reorders — on load we map each saved spark back into the current
// watchlist slot by symbol match. Full SparklineData (~1480B × 8 = ~12KB)
// is written only on full-rotation flush, same cadence as the quote cache.
static const uint8_t STOCK_SPARKS_VER = 1;

// File-scope scratch to keep the 12KB blob off the task stack.
static char           _spSymScratch[STOCK_MAX_SYMBOLS][STOCK_SYMBOL_LEN];
static SparklineData  _spDataScratch[STOCK_MAX_SYMBOLS];

void nvsLoadStockSparks(SparklineData* out, const StockWatchlist& wl) {
    for (uint8_t i = 0; i < STOCK_MAX_SYMBOLS; i++) out[i].valid = false;

    uint8_t ver = prefs.getUChar("sp_ver", 0);
    if (ver != STOCK_SPARKS_VER) return;

    size_t symsSz  = sizeof(_spSymScratch);
    size_t sparksSz = sizeof(_spDataScratch);
    if (prefs.getBytes("sp_syms", _spSymScratch, symsSz) != symsSz) return;
    if (prefs.getBytes("sp_data", _spDataScratch, sparksSz) != sparksSz) return;

    uint8_t placed = 0;
    for (uint8_t s = 0; s < STOCK_MAX_SYMBOLS; s++) {
        if (!_spDataScratch[s].valid) continue;
        if (_spSymScratch[s][0] == '\0') continue;
        for (uint8_t w = 0; w < wl.count; w++) {
            if (strncmp(_spSymScratch[s], wl.symbols[w], STOCK_SYMBOL_LEN) == 0) {
                out[w] = _spDataScratch[s];
                out[w].lastUpdate = 0;   // millis() doesn't survive reboot
                placed++;
                break;
            }
        }
    }
    Serial.printf("[NVS] Stock sparks loaded: %u/%u\n",
                  (unsigned)placed, (unsigned)wl.count);
}

void nvsSaveStockSparks(const SparklineData* sparks, const StockWatchlist& wl) {
    memset(_spSymScratch, 0, sizeof(_spSymScratch));
    memset(_spDataScratch, 0, sizeof(_spDataScratch));

    for (uint8_t i = 0; i < wl.count && i < STOCK_MAX_SYMBOLS; i++) {
        if (!sparks[i].valid) continue;
        strncpy(_spSymScratch[i], wl.symbols[i], STOCK_SYMBOL_LEN - 1);
        _spSymScratch[i][STOCK_SYMBOL_LEN - 1] = '\0';
        _spDataScratch[i] = sparks[i];
    }

    prefs.putUChar("sp_ver", STOCK_SPARKS_VER);
    prefs.putBytes("sp_syms", _spSymScratch, sizeof(_spSymScratch));
    prefs.putBytes("sp_data", _spDataScratch, sizeof(_spDataScratch));
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

void nvsFactoryReset() {
    prefs.clear();
    Serial.println("[NVS] Factory reset — all keys erased");
}

#include "nvs_storage.h"
#include <Preferences.h>
#include <Arduino.h>

static Preferences prefs;
static const char* NVS_NAMESPACE = "lemon";

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
    String p = prefs.getString("wifi_pass", "");
    strncpy(ssid, s.c_str(), ssidLen - 1);
    ssid[ssidLen - 1] = '\0';
    strncpy(pass, p.c_str(), passLen - 1);
    pass[passLen - 1] = '\0';
}

void nvsSaveWifi(const char* ssid, const char* pass) {
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", pass);
    Serial.printf("[NVS] WiFi saved: %s\n", ssid);
}

void nvsForgetWifi() {
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pass");
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

// ── Factory Reset ──

void nvsFactoryReset() {
    prefs.clear();
    Serial.println("[NVS] Factory reset — all keys erased");
}

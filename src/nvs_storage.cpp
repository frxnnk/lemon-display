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
    return prefs.getUChar("layout", 0);
}

void nvsSetLayout(uint8_t idx) {
    if (idx > 2) idx = 0;
    prefs.putUChar("layout", idx);
}

// ── Polymarket Predictions ──

#include "data_models.h"

void nvsLoadPolyStats(PolyStats& stats) {
    stats.wins      = prefs.getUShort("pm_wins", 0);
    stats.losses    = prefs.getUShort("pm_losses", 0);
    stats.pending   = 0;
    stats.streak    = prefs.getUShort("pm_streak", 0);
    stats.bestStreak = prefs.getUShort("pm_best", 0);
}

void nvsSavePolyStats(const PolyStats& stats) {
    prefs.putUShort("pm_wins", stats.wins);
    prefs.putUShort("pm_losses", stats.losses);
    prefs.putUShort("pm_streak", stats.streak);
    prefs.putUShort("pm_best", stats.bestStreak);
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
    pred.resolved  = 0;  // Active = pending
}

void nvsSavePolyPrediction(const PolyPrediction& pred) {
    prefs.putUChar("pm_active", 1);
    prefs.putString("pm_cond", pred.conditionId);
    prefs.putUChar("pm_yes", pred.chosenYes ? 1 : 0);
    prefs.putFloat("pm_prob", pred.probAtBet);
    prefs.putULong("pm_ts", pred.timestamp);
    Serial.printf("[NVS] Poly prediction saved: %s @ %.0f%%\n",
                  pred.chosenYes ? "YES" : "NO", pred.probAtBet * 100);
}

void nvsClearPolyPrediction() {
    prefs.putUChar("pm_active", 0);
    prefs.remove("pm_cond");
    prefs.remove("pm_yes");
    prefs.remove("pm_prob");
    prefs.remove("pm_ts");
    Serial.println("[NVS] Poly prediction cleared");
}

// ── Factory Reset ──

void nvsFactoryReset() {
    prefs.clear();
    Serial.println("[NVS] Factory reset — all keys erased");
}

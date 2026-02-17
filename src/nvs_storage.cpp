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

// ── Factory Reset ──

void nvsFactoryReset() {
    prefs.clear();
    Serial.println("[NVS] Factory reset — all keys erased");
}

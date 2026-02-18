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

// ── Supabase Pairing ──

bool nvsHasPairing() {
    return prefs.isKey("supa_uid") && prefs.getString("supa_uid", "").length() > 0;
}

void nvsSavePairing(const char* userId, const char* tag) {
    prefs.putString("supa_uid", userId);
    prefs.putString("supa_tag", tag);
    prefs.putUChar("supa_paired", 1);
    Serial.printf("[NVS] Pairing saved: %s @%s\n", userId, tag);
}

void nvsLoadPairing(char* userId, size_t uidLen, char* tag, size_t tagLen) {
    String u = prefs.getString("supa_uid", "");
    String t = prefs.getString("supa_tag", "");
    strncpy(userId, u.c_str(), uidLen - 1);
    userId[uidLen - 1] = '\0';
    strncpy(tag, t.c_str(), tagLen - 1);
    tag[tagLen - 1] = '\0';
}

void nvsForgetPairing() {
    prefs.remove("supa_uid");
    prefs.remove("supa_tag");
    prefs.putUChar("supa_paired", 0);
    Serial.println("[NVS] Pairing erased");
}

// ── Device ID ──

bool nvsHasDeviceId() {
    return prefs.isKey("supa_devid") && prefs.getString("supa_devid", "").length() > 0;
}

void nvsSaveDeviceId(const char* deviceId) {
    prefs.putString("supa_devid", deviceId);
    Serial.printf("[NVS] Device ID saved: %s\n", deviceId);
}

void nvsLoadDeviceId(char* deviceId, size_t len) {
    String d = prefs.getString("supa_devid", "");
    strncpy(deviceId, d.c_str(), len - 1);
    deviceId[len - 1] = '\0';
}

// ── Pairing Code ──

void nvsSavePairingCode(const char* code) {
    prefs.putString("supa_code", code);
}

void nvsLoadPairingCode(char* code, size_t len) {
    String c = prefs.getString("supa_code", "------");
    strncpy(code, c.c_str(), len - 1);
    code[len - 1] = '\0';
}

// ── Factory Reset ──

void nvsFactoryReset() {
    prefs.clear();
    Serial.println("[NVS] Factory reset — all keys erased");
}

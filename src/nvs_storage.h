#pragma once

#include <cstdint>
#include <cstddef>

// ── NVS Persistent Storage ──
// Uses ESP32 Preferences library (key-value in NVS flash)

void nvsInit();

// WiFi credentials
bool     nvsHasWifi();
void     nvsLoadWifi(char* ssid, size_t ssidLen, char* pass, size_t passLen);
void     nvsSaveWifi(const char* ssid, const char* pass);
void     nvsForgetWifi();

// Display
uint8_t  nvsGetBrightness();
void     nvsSetBrightness(uint8_t val);

// Sound
bool     nvsGetSoundEnabled();
void     nvsSetSoundEnabled(bool on);

// Alerts
bool     nvsGetAlertEnabled();
void     nvsSetAlertEnabled(bool on);

// Time format (true = 24h, false = 12h AM/PM)
bool     nvsGet24hFormat();
void     nvsSet24hFormat(bool on);

// Dashboard layout preset (0=standard, 1=btc_focus, 2=compact)
uint8_t  nvsGetLayout();
void     nvsSetLayout(uint8_t idx);

// ── Supabase pairing ──
bool     nvsHasPairing();
void     nvsSavePairing(const char* userId, const char* tag);
void     nvsLoadPairing(char* userId, size_t uidLen, char* tag, size_t tagLen);
void     nvsForgetPairing();

// Device ID (persists across unpairing)
bool     nvsHasDeviceId();
void     nvsSaveDeviceId(const char* deviceId);
void     nvsLoadDeviceId(char* deviceId, size_t len);

// Pairing code (shown on screen)
void     nvsSavePairingCode(const char* code);
void     nvsLoadPairingCode(char* code, size_t len);

// Factory reset — erases all NVS keys
void     nvsFactoryReset();

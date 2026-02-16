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

// Factory reset — erases all NVS keys
void     nvsFactoryReset();

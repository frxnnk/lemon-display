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

// Product mode (true = Pro, false = Normal)
bool     nvsGetProMode();
void     nvsSetProMode(bool on);

// Polymarket prediction stats
struct PolyStats;
struct PolyPrediction;
void     nvsLoadPolyStats(uint8_t periodIdx, PolyStats& stats);
void     nvsSavePolyStats(uint8_t periodIdx, const PolyStats& stats);
bool     nvsHasPolyPrediction();
void     nvsLoadPolyPrediction(PolyPrediction& pred);
void     nvsSavePolyPrediction(const PolyPrediction& pred);
void     nvsClearPolyPrediction();

// Factory reset — erases all NVS keys
void     nvsFactoryReset();

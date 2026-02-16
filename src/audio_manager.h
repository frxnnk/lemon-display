#pragma once

#include <cstdint>

void audioSetup();

// Global enable/disable (persisted via NVS externally)
void audioSetEnabled(bool on);
bool audioIsEnabled();

// Tone primitives
void playTone(uint16_t freqHz, uint16_t durationMs);

// Pre-built alert sounds
void playAlertUp();     // Ascending two-tone: 800Hz -> 1200Hz
void playAlertDown();   // Descending two-tone: 1200Hz -> 800Hz
void playStartup();     // Three-note chime: C5-E5-G5
void playTap();         // Short click feedback: 2000Hz, 30ms

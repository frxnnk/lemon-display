#pragma once

#include <Arduino.h>

// ── WiFi network scan result ──
struct WiFiNetwork {
    char ssid[33];
    int32_t rssi;
    uint8_t encType;  // WIFI_AUTH_OPEN, etc.
};

// ── Setup / Loop ──
void wifiSetup(const char* ssid, const char* password);  // Connect with given creds
void wifiLoop();     // Call periodically for auto-reconnect
bool wifiConnected();

// ── Async scan ──
void wifiStartScan();
int  wifiScanComplete();  // Returns: -1 = in progress, -2 = error, >=0 = count
int  wifiGetScanResults(WiFiNetwork* results, int maxResults);  // Fills array sorted by RSSI

// ── Async connect ──
void wifiConnectAsync(const char* ssid, const char* password);
bool wifiConnecting();         // Still trying?
bool wifiConnectSucceeded();   // Connected after async begin?
bool wifiConnectFailed();      // Timed out or failed?

// ── Info ──
const char* wifiSSID();
int32_t     wifiRSSI();
String      wifiIP();

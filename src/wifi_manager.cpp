#include "wifi_manager.h"
#include <WiFi.h>
#include <esp_netif.h>

static unsigned long lastReconnectAttempt = 0;
static const unsigned long RECONNECT_INTERVAL = 10000; // 10s between retries

// Stored credentials for auto-reconnect
static char storedSSID[33] = {0};
static char storedPass[65] = {0};
static bool everConnected = false;  // Only reconnect if we connected successfully at least once

// Async connect state
static unsigned long connectStartMs = 0;
static const unsigned long CONNECT_TIMEOUT_MS = 15000;
static bool asyncConnecting = false;

void wifiSetup(const char* ssid, const char* password) {
    strncpy(storedSSID, ssid, sizeof(storedSSID) - 1);
    strncpy(storedPass, password, sizeof(storedPass) - 1);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);  // Don't auto-reconnect until confirmed working
    WiFi.begin(ssid, password);

    Serial.print("[WiFi] Connecting");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        // Force Google DNS to bypass ISP DNS blocking (Telefonica blocks Polymarket)
        IPAddress dns1(8, 8, 8, 8);
        IPAddress dns2(1, 1, 1, 1);
        WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);
        Serial.println("[WiFi] DNS set to 8.8.8.8 / 1.1.1.1");
        WiFi.setAutoReconnect(true);  // Only enable after successful connection
        everConnected = true;
    } else {
        Serial.println("[WiFi] Connection failed");
        WiFi.disconnect(true);  // Stop trying
    }
}

void wifiLoop() {
    if (WiFi.status() == WL_CONNECTED) return;
    if (storedSSID[0] == '\0') return;  // No credentials stored
    if (!everConnected) return;  // Never connected — don't retry with possibly bad creds

    unsigned long now = millis();
    if (now - lastReconnectAttempt >= RECONNECT_INTERVAL) {
        lastReconnectAttempt = now;
        Serial.println("[WiFi] Reconnecting...");
        WiFi.disconnect();
        WiFi.begin(storedSSID, storedPass);
    }
}

bool wifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

// ── Async scan ──

void wifiStartScan() {
    WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    WiFi.scanNetworks(true);  // true = async
    Serial.println("[WiFi] Scan started (async)");
}

int wifiScanComplete() {
    return WiFi.scanComplete();
}

int wifiGetScanResults(WiFiNetwork* results, int maxResults) {
    int found = WiFi.scanComplete();
    if (found <= 0) return 0;

    int count = min(found, maxResults);

    // Fill array
    for (int i = 0; i < count; i++) {
        strncpy(results[i].ssid, WiFi.SSID(i).c_str(), 32);
        results[i].ssid[32] = '\0';
        results[i].rssi = WiFi.RSSI(i);
        results[i].encType = WiFi.encryptionType(i);
    }

    // Sort by RSSI descending (strongest first) — simple insertion sort
    for (int i = 1; i < count; i++) {
        WiFiNetwork temp = results[i];
        int j = i - 1;
        while (j >= 0 && results[j].rssi < temp.rssi) {
            results[j + 1] = results[j];
            j--;
        }
        results[j + 1] = temp;
    }

    // Remove duplicates (keep strongest)
    int unique = 0;
    for (int i = 0; i < count; i++) {
        bool dup = false;
        for (int j = 0; j < unique; j++) {
            if (strcmp(results[j].ssid, results[i].ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup && results[i].ssid[0] != '\0') {
            if (unique != i) results[unique] = results[i];
            unique++;
        }
    }

    WiFi.scanDelete();
    return unique;
}

// ── Async connect ──

void wifiConnectAsync(const char* ssid, const char* password) {
    strncpy(storedSSID, ssid, sizeof(storedSSID) - 1);
    strncpy(storedPass, password, sizeof(storedPass) - 1);

    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);

    asyncConnecting = true;
    connectStartMs = millis();
    Serial.printf("[WiFi] Async connect to '%s'\n", ssid);
}

bool wifiConnecting() {
    if (!asyncConnecting) return false;
    if (WiFi.status() == WL_CONNECTED) {
        // Force Google DNS to bypass ISP DNS blocking
        IPAddress dns1(8, 8, 8, 8);
        IPAddress dns2(1, 1, 1, 1);
        WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);
        asyncConnecting = false;
        return false;
    }
    if (millis() - connectStartMs >= CONNECT_TIMEOUT_MS) {
        asyncConnecting = false;
        return false;
    }
    return true;
}

bool wifiConnectSucceeded() {
    return !asyncConnecting && WiFi.status() == WL_CONNECTED;
}

bool wifiConnectFailed() {
    return !asyncConnecting && WiFi.status() != WL_CONNECTED && connectStartMs > 0;
}

// ── Info ──

const char* wifiSSID() {
    return storedSSID;
}

int32_t wifiRSSI() {
    return WiFi.RSSI();
}

String wifiIP() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "—";
}

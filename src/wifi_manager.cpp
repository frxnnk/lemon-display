#include "wifi_manager.h"
#include <WiFi.h>
#include <esp_netif.h>

static unsigned long lastReconnectAttempt = 0;
static unsigned long reconnectInterval = 10000; // Starts at 10s, doubles up to 60s
// 60 s y no 5 min: el aparato vive enchufado y reintentar no cuesta nada. Con
// el tope en 5 min, un bache de diez segundos podia dejarlo desconectado cinco
// minutos porque le tocaba esperar.
static const unsigned long RECONNECT_MAX = 60000;

// Stored credentials for auto-reconnect
static char storedSSID[33] = {0};
static char storedPass[65] = {0};
static bool everConnected = false;  // Only reconnect if we connected successfully at least once
static bool dnsApplied = false;

// Async connect state
static unsigned long connectStartMs = 0;
static const unsigned long CONNECT_TIMEOUT_MS = 15000;
static bool asyncConnecting = false;

// Ultimo RSSI muestreado con la asociacion viva. En el evento de desconexion
// WiFi.RSSI() ya no tiene enlace que medir: lo que discrimina es la senal que
// habia justo antes de la caida.
static int32_t s_lastRssi = 0;
static unsigned long s_lastRssiSample = 0;
static bool s_eventsHooked = false;

// El wifi_err_reason_t del SDK es el dato mas discriminante de todos y sin
// este manejador se tira: 200/201 apuntan al receptor y a la senal, 2/3 a que
// el AP lo echo, 8 a que el firmware se fue solo.
static void hookWifiEvents() {
    if (s_eventsHooked) return;
    s_eventsHooked = true;
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        Serial.printf("[WiFi] caida: reason=%d rssi=%ld t=%lu\n",
                      (int)info.wifi_sta_disconnected.reason,
                      (long)s_lastRssi, (unsigned long)millis());
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

static void applyPublicDns() {
    IPAddress dns1(8, 8, 8, 8);
    IPAddress dns2(1, 1, 1, 1);
    IPAddress ip = WiFi.localIP();
    IPAddress gateway = WiFi.gatewayIP();
    IPAddress subnet = WiFi.subnetMask();

    WiFi.config(ip, gateway, subnet, dns1, dns2);
    dnsApplied = true;

    String ipStr = ip.toString();
    String gwStr = gateway.toString();
    Serial.printf("[WiFi] DNS override: ip=%s gw=%s dns=8.8.8.8/1.1.1.1\n",
                  ipStr.c_str(), gwStr.c_str());
}

void wifiSetup(const char* ssid, const char* password) {
    strncpy(storedSSID, ssid, sizeof(storedSSID) - 1);
    strncpy(storedPass, password, sizeof(storedPass) - 1);
    dnsApplied = false;

    WiFi.mode(WIFI_STA);
    hookWifiEvents();
    // El modem sleep viene activado por default y es causa conocida de beacons
    // perdidos y desconexiones. Este aparato vive enchufado a la pared: no
    // tiene ningun motivo para ahorrar energia.
    WiFi.setSleep(false);
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
        applyPublicDns();
        WiFi.setAutoReconnect(true);  // Only enable after successful connection
        everConnected = true;
    } else {
        Serial.println("[WiFi] Connection failed");
        dnsApplied = false;
        WiFi.disconnect(true);  // Stop trying
    }
}

void wifiLoop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!dnsApplied) {
            applyPublicDns();
        }
        // Se muestrea seguido para que el manejador de desconexion tenga la
        // senal de justo antes de la caida, no la de hace media hora.
        const unsigned long nowRssi = millis();
        if (nowRssi - s_lastRssiSample >= 5000) {
            s_lastRssiSample = nowRssi;
            s_lastRssi = WiFi.RSSI();
        }
        // Reset backoff on successful connection
        reconnectInterval = 10000;
        everConnected = true;
        return;
    }
    dnsApplied = false;
    if (storedSSID[0] == '\0') return;  // No credentials stored
    if (!everConnected) return;  // Never connected — don't retry with possibly bad creds

    unsigned long now = millis();
    if (now - lastReconnectAttempt >= reconnectInterval) {
        lastReconnectAttempt = now;
        Serial.printf("[WiFi] Reconnecting (backoff %lums)...\n", reconnectInterval);
        WiFi.disconnect();
        dnsApplied = false;
        WiFi.begin(storedSSID, storedPass);

        // Exponential backoff: 10s → 20s → 40s → ... → 5min max
        reconnectInterval = reconnectInterval * 2;
        if (reconnectInterval > RECONNECT_MAX) reconnectInterval = RECONNECT_MAX;
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
    dnsApplied = false;

    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    hookWifiEvents();
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);

    asyncConnecting = true;
    connectStartMs = millis();
    Serial.printf("[WiFi] Async connect to '%s'\n", ssid);
}

bool wifiConnecting() {
    if (!asyncConnecting) return false;
    if (WiFi.status() == WL_CONNECTED) {
        applyPublicDns();
        everConnected = true;
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

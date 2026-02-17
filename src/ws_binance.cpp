#include "ws_binance.h"
#include "config.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ── Circular buffer for 1-minute kline close prices ──
static float    ringBuf[SPARKLINE_POINTS];
static uint16_t ringHead  = 0;   // Next write position
static uint16_t ringCount = 0;   // How many valid entries

// ── Current kline tracking ──
static unsigned long long currentKlineOpenTime = 0;

// ── Latest price from stream ──
static float latestPrice    = 0.0f;
static bool  hasPrice       = false;

// ── WebSocket client ──
static WebSocketsClient ws;
static bool wsConnected = false;

// ── Ring buffer helpers ──
static void ringPush(float val) {
    ringBuf[ringHead] = val;
    ringHead = (ringHead + 1) % SPARKLINE_POINTS;
    if (ringCount < SPARKLINE_POINTS) ringCount++;
}

static void ringUpdateLast(float val) {
    if (ringCount == 0) {
        ringPush(val);
        return;
    }
    // Update the most recently pushed value
    uint16_t lastIdx = (ringHead == 0) ? (SPARKLINE_POINTS - 1) : (ringHead - 1);
    ringBuf[lastIdx] = val;
}

// ── Parse kline JSON message ──
static void parseKline(uint8_t* payload, size_t length) {
    // Use filter to only extract what we need
    JsonDocument filter;
    filter["k"]["t"] = true;   // Kline open time
    filter["k"]["c"] = true;   // Close price (string)
    filter["k"]["x"] = true;   // Is this kline closed?

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length,
        DeserializationOption::Filter(filter));

    if (err) {
        Serial.printf("[WS] JSON parse error: %s\n", err.c_str());
        return;
    }

    JsonObject k = doc["k"];
    unsigned long long openTime = k["t"] | (unsigned long long)0;
    const char* closeStr = k["c"] | (const char*)nullptr;
    bool isClosed = k["x"] | false;

    if (!closeStr || openTime == 0) return;

    float closePrice = atof(closeStr);
    if (closePrice <= 0) return;

    // Always update latest price
    latestPrice = closePrice;
    hasPrice = true;

    if (openTime != currentKlineOpenTime) {
        // New kline started — push new entry
        ringPush(closePrice);
        currentKlineOpenTime = openTime;
    } else {
        // Same kline — update current candle's close
        ringUpdateLast(closePrice);
    }
}

// ── WebSocket event handler ──
static void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            wsConnected = true;
            Serial.printf("[WS] Connected to %s\n", BINANCE_WS_PATH);
            break;

        case WStype_DISCONNECTED:
            wsConnected = false;
            Serial.println("[WS] Disconnected");
            break;

        case WStype_TEXT:
            parseKline(payload, length);
            break;

        case WStype_PONG:
            // Keep-alive acknowledged
            break;

        default:
            break;
    }
}

// ── Public API ──

void wsBinanceSetup() {
    ws.beginSSL(BINANCE_WS_HOST, BINANCE_WS_PORT, BINANCE_WS_PATH);
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(WS_RECONNECT_MS);
    ws.enableHeartbeat(WS_PING_MS, WS_PONG_TIMEOUT, WS_DISCONNECT_CNT);
    Serial.println("[WS] Setup complete, connecting...");
}

void wsBinanceLoop() {
    ws.loop();
}

void wsBinanceStop() {
    ws.disconnect();
    wsConnected = false;
    Serial.println("[WS] Stopped");
}

bool wsBinanceConnected() {
    return wsConnected;
}

float wsBinanceGetPrice() {
    return latestPrice;
}

bool wsBinanceHasPrice() {
    return hasPrice;
}

void wsBinanceGetSparkline(SparklineData& out) {
    if (ringCount == 0) {
        out.valid = false;
        return;
    }

    // Linearize: oldest first
    uint16_t start;
    if (ringCount < SPARKLINE_POINTS) {
        start = 0;  // Buffer not yet full, data starts at 0
    } else {
        start = ringHead;  // Buffer full, oldest is at ringHead
    }

    out.count = ringCount;
    out.minVal = 1e12f;
    out.maxVal = -1e12f;

    for (uint16_t i = 0; i < ringCount; i++) {
        uint16_t idx;
        if (ringCount < SPARKLINE_POINTS) {
            idx = i;
        } else {
            idx = (start + i) % SPARKLINE_POINTS;
        }
        float val = ringBuf[idx];
        out.points[i] = val;
        if (val < out.minVal) out.minVal = val;
        if (val > out.maxVal) out.maxVal = val;
    }

    out.valid = true;
    out.lastUpdate = millis();
}

bool wsBinanceBackfill() {
    Serial.println("[WS] Backfill: fetching 96 x 1m klines...");

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(10000);

    String url = String(BINANCE_KLINES_EP) + "?symbol=BTCUSDT&interval=1m&limit=96";

    if (!http.begin(client, url)) {
        Serial.println("[WS] Backfill: HTTP begin failed");
        http.end();
        return false;
    }

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[WS] Backfill: HTTP %d\n", code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    // Parse array of arrays: [[openTime, open, high, low, close, ...], ...]
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("[WS] Backfill: JSON error: %s\n", err.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) {
        Serial.println("[WS] Backfill: empty array");
        return false;
    }

    // Reset ring buffer
    ringHead = 0;
    ringCount = 0;

    int loaded = 0;
    unsigned long long lastOpenTime = 0;

    for (JsonArray kline : arr) {
        // Index 0 = open time, index 4 = close price (string)
        unsigned long long openTime = kline[0].as<unsigned long long>();
        const char* closeStr = kline[4].as<const char*>();
        if (!closeStr) continue;

        float closePrice = atof(closeStr);
        if (closePrice <= 0) continue;

        ringPush(closePrice);
        lastOpenTime = openTime;
        loaded++;
    }

    if (loaded > 0 && lastOpenTime > 0) {
        currentKlineOpenTime = lastOpenTime;
        latestPrice = ringBuf[(ringHead == 0) ? (SPARKLINE_POINTS - 1) : (ringHead - 1)];
        hasPrice = true;
    }

    Serial.printf("[WS] Backfill: %d klines loaded, latest=$%.0f\n", loaded, latestPrice);
    return loaded > 0;
}

#include "feed_client.h"
#include "ferced_config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cstring>

static FeedItem _items[FEED_MAX_ITEMS];
static uint8_t  _count = 0;

uint8_t feedCount() { return _count; }

const FeedItem* feedItem(uint8_t index) {
    if (index >= _count) return nullptr;
    return &_items[index];
}

static void copyField(char* dst, size_t dstLen, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dstLen - 1);
    dst[dstLen - 1] = '\0';
}

static FeedOrigin originFrom(const char* o) {
    if (!o) return FEED_FROM_RSS;
    if (strcmp(o, "x") == 0) return FEED_FROM_X;
    if (strcmp(o, "trend") == 0) return FEED_FROM_TREND;
    return FEED_FROM_RSS;
}

FeedResult feedFetch() {
    const char* url = FEED_ENDPOINT;
    const bool secure = strncmp(url, "https://", 8) == 0;

    WiFiClient plain;
    WiFiClientSecure tls;
    if (secure) tls.setInsecure();

    HTTPClient http;
    http.setTimeout(12000);
    http.setUserAgent("ferced-display/1.0");

    bool begun = secure ? http.begin(tls, url) : http.begin(plain, url);
    if (!begun) {
        Serial.println("[Feed] no se pudo abrir la conexion");
        return _count > 0 ? FEED_STALE_CACHE : FEED_FAILED;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Feed] HTTP %d; conservo cache\n", code);
        http.end();
        return _count > 0 ? FEED_STALE_CACHE : FEED_FAILED;
    }

    // getString() decodifica el framing de chunks; getStream() entrega el
    // stream crudo y ArduinoJson se atraganta si el servidor responde
    // chunked. El proxy ya manda Content-Length, pero esto lo deja a salvo
    // de cualquier servidor.
    const String payload = http.getString();
    http.end();

    if (payload.isEmpty()) {
        Serial.println("[Feed] cuerpo vacio");
        return _count > 0 ? FEED_STALE_CACHE : FEED_FAILED;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, payload);

    if (err) {
        Serial.printf("[Feed] JSON invalido: %s\n", err.c_str());
        return _count > 0 ? FEED_STALE_CACHE : FEED_FAILED;
    }

    JsonArray items = doc["items"].as<JsonArray>();
    if (items.isNull()) {
        Serial.println("[Feed] respuesta sin items");
        return _count > 0 ? FEED_STALE_CACHE : FEED_FAILED;
    }

    uint8_t n = 0;
    for (JsonObject it : items) {
        if (n >= FEED_MAX_ITEMS) break;
        const char* text = it["t"];
        if (!text || !text[0]) continue;

        copyField(_items[n].text, FEED_TEXT_LEN, text);
        copyField(_items[n].author, FEED_AUTHOR_LEN, it["a"] | "");
        copyField(_items[n].handle, FEED_HANDLE_LEN, it["h"] | "");
        _items[n].epoch = it["ts"] | 0UL;
        _items[n].origin = originFrom(it["o"]);
        n++;
    }

    if (n == 0) {
        Serial.println("[Feed] no entro ningun item utilizable");
        return _count > 0 ? FEED_STALE_CACHE : FEED_FAILED;
    }

    _count = n;
    Serial.printf("[Feed] %u items\n", _count);
    return FEED_UPDATED;
}

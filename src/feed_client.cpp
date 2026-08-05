#include "feed_client.h"
#include "ferced_config.h"
#include "ui_ferced.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cstring>

static void addAuth(HTTPClient& http);

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

// Un solo buffer: se rota un item cada 17 s, asi que no hace falta cachear
// varias. La key evita rebajar la misma imagen si el item se repite.
static uint16_t _imgBuf[FEED_IMG_PIXELS];
static char     _imgBufKey[FEED_IMGKEY_LEN] = {0};

const uint16_t* feedFetchImage(const char* key) {
    if (!key || !key[0]) return nullptr;
    if (strncmp(_imgBufKey, key, FEED_IMGKEY_LEN) == 0) return _imgBuf;

    char url[192];
    const char* base = FEED_ENDPOINT;
    const char* slash = strstr(base, "/v1/feed");
    if (!slash) return nullptr;
    // Las metricas de animacion viajan tambien aca: una imagen se pide cada
    // ~35 s, contra los 10 min del feed. Sirve para iterar el rendimiento.
    const UiFrameStats st = uiAnimStats();
    snprintf(url, sizeof(url), "%.*s/v1/img?k=%s&fr=%u&avg=%u&max=%u",
             (int)(slash - base), base, key,
             st.frames, st.avgUs100, st.worstUs100);

    const bool secure = strncmp(url, "https://", 8) == 0;
    WiFiClient plain;
    WiFiClientSecure tls;
    if (secure) tls.setInsecure();

    HTTPClient http;
    http.setTimeout(8000);
    if (!(secure ? http.begin(tls, url) : http.begin(plain, url))) return nullptr;
    addAuth(http);

    if (http.GET() != HTTP_CODE_OK) { http.end(); return nullptr; }

    const int expected = FEED_IMG_PIXELS * 2;
    if (http.getSize() != expected) {
        Serial.printf("[Feed] imagen de %d bytes, esperaba %d\n", http.getSize(), expected);
        http.end();
        return nullptr;
    }

    const int got = http.getStream().readBytes((uint8_t*)_imgBuf, expected);
    http.end();
    if (got != expected) {
        _imgBufKey[0] = '\0';
        return nullptr;
    }

    strncpy(_imgBufKey, key, FEED_IMGKEY_LEN - 1);
    _imgBufKey[FEED_IMGKEY_LEN - 1] = '\0';
    return _imgBuf;
}

static void addAuth(HTTPClient& http) {
    if (sizeof(FEED_TOKEN) > 1) {
        http.addHeader("Authorization", "Bearer " FEED_TOKEN);
    }
}

FeedResult feedFetch(uint16_t frames, uint16_t avgUs100, uint16_t worstUs100,
                     bool fresh) {
    // Las metricas de animacion viajan en el pedido del feed: no hace falta
    // consola serie para saber a que framerate corre el aparato.
    char url[256];
    snprintf(url, sizeof(url), "%s&fr=%u&avg=%u&max=%u%s",
             FEED_ENDPOINT, frames, avgUs100, worstUs100,
             fresh ? "&fresh=1" : "");
    const bool secure = strncmp(url, "https://", 8) == 0;

    WiFiClient plain;
    WiFiClientSecure tls;
    if (secure) tls.setInsecure();

    HTTPClient http;
    http.setTimeout(12000);
    http.setUserAgent("ferced-display/1.0");

    bool begun = secure ? http.begin(tls, url) : http.begin(plain, url);
    if (begun) addAuth(http);
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
        copyField(_items[n].imgKey, FEED_IMGKEY_LEN, it["k"] | "");
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


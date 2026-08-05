#include "padel_client.h"
#include "feed_client.h"
#include "ferced_config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cstring>

static PadelMatch _matches[PADEL_MAX_MATCHES];
static uint8_t    _matchCount = 0;
static PadelTour  _tours[PADEL_MAX_TOURS];
static uint8_t    _tourCount = 0;
static PadelTour  _live;
static bool       _hasLive = false;

uint8_t           padelMatchCount() { return _matchCount; }
uint8_t           padelTourCount() { return _tourCount; }
const PadelMatch* padelMatch(uint8_t i) { return i < _matchCount ? &_matches[i] : nullptr; }
const PadelTour*  padelTour(uint8_t i) { return i < _tourCount ? &_tours[i] : nullptr; }
const PadelTour*  padelLive() { return _hasLive ? &_live : nullptr; }

uint8_t padelScreenCount() {
    return (uint8_t)((_hasLive ? 1 : 0) + _matchCount + _tourCount);
}

static void copyField(char* dst, size_t dstLen, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dstLen - 1);
    dst[dstLen - 1] = '\0';
}

static void leerTorneo(PadelTour& t, JsonObjectConst o) {
    memset(&t, 0, sizeof(t));
    copyField(t.name, sizeof(t.name), o["nombre"] | "");
    copyField(t.cat, sizeof(t.cat), o["cat"] | "");
    copyField(t.city, sizeof(t.city), o["ciudad"] | "");
    copyField(t.country, sizeof(t.country), o["pais"] | "");
    copyField(t.rango, sizeof(t.rango), o["rango"] | "");
    t.faltan = o["faltan"] | 0;
}

PadelResult padelFetch() {
    // El endpoint sale del mismo host que el feed: el aparato nunca arma una
    // URL de un tercero, solo cambia la ruta de su propio proxy.
    char url[192];
    const char* base = FEED_ENDPOINT;
    const char* slash = strstr(base, "/v1/feed");
    if (!slash) return PADEL_FAILED;
    snprintf(url, sizeof(url), "%.*s/v1/padel", (int)(slash - base), base);

    const bool secure = strncmp(url, "https://", 8) == 0;
    WiFiClient plain;
    WiFiClientSecure tls;
    if (secure) tls.setInsecure();

    HTTPClient http;
    http.setTimeout(12000);
    http.setUserAgent("ferced-display/1.0");

    const bool begun = secure ? http.begin(tls, url) : http.begin(plain, url);
    if (!begun) {
        Serial.println("[Padel] no se pudo abrir la conexion");
        return _matchCount || _tourCount ? PADEL_STALE_CACHE : PADEL_FAILED;
    }
    if (sizeof(FEED_TOKEN) > 1) {
        http.addHeader("Authorization", "Bearer " FEED_TOKEN);
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Padel] HTTP %d; conservo cache\n", code);
        http.end();
        return _matchCount || _tourCount ? PADEL_STALE_CACHE : PADEL_FAILED;
    }

    // getString() decodifica el framing de chunks; getStream() lo entrega
    // crudo y ArduinoJson se atraganta. Misma trampa que en el feed.
    const String payload = http.getString();
    http.end();

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("[Padel] JSON invalido: %s\n", err.c_str());
        return _matchCount || _tourCount ? PADEL_STALE_CACHE : PADEL_FAILED;
    }

    _hasLive = false;
    if (JsonObjectConst t = doc["torneo"].as<JsonObjectConst>()) {
        leerTorneo(_live, t);
        _live.live = true;
        _live.day = doc["dia"] | 0;
        _live.days = doc["dias"] | 0;
        _hasLive = _live.name[0] != '\0';
    }

    uint8_t n = 0;
    for (JsonObjectConst m : doc["partidos"].as<JsonArrayConst>()) {
        if (n >= PADEL_MAX_MATCHES) break;
        PadelMatch& x = _matches[n];
        memset(&x, 0, sizeof(x));
        copyField(x.a1, sizeof(x.a1), m["a1"] | "");
        copyField(x.b1, sizeof(x.b1), m["b1"] | "");
        // Sin las dos parejas no hay partido que mostrar.
        if (!x.a1[0] || !x.b1[0]) continue;

        copyField(x.a2, sizeof(x.a2), m["a2"] | "");
        copyField(x.b2, sizeof(x.b2), m["b2"] | "");
        copyField(x.time, sizeof(x.time), m["hora"] | "");
        copyField(x.court, sizeof(x.court), m["cancha"] | "");
        copyField(x.round, sizeof(x.round), m["fase"] | "");
        copyField(x.seedA, sizeof(x.seedA), m["sa"] | "");
        copyField(x.seedB, sizeof(x.seedB), m["sb"] | "");
        copyField(x.scoreA, sizeof(x.scoreA), m["ra"] | "");
        copyField(x.scoreB, sizeof(x.scoreB), m["rb"] | "");

        const char* g = m["gen"] | "";
        x.gender = g[0];

        const char* est = m["est"] | "";
        x.state = strcmp(est, "listo") == 0 ? 2 : (strcmp(est, "jugando") == 0 ? 1 : 0);
        n++;
    }
    _matchCount = n;

    n = 0;
    for (JsonObjectConst t : doc["proximos"].as<JsonArrayConst>()) {
        if (n >= PADEL_MAX_TOURS) break;
        leerTorneo(_tours[n], t);
        if (!_tours[n].name[0]) continue;
        n++;
    }
    _tourCount = n;

    if (!_hasLive && _matchCount == 0 && _tourCount == 0) {
        Serial.println("[Padel] respuesta sin contenido utilizable");
        return PADEL_FAILED;
    }

    Serial.printf("[Padel] %s, %u partidos, %u proximos\n",
                  _hasLive ? _live.name : "sin torneo en juego",
                  _matchCount, _tourCount);
    return PADEL_UPDATED;
}

#include "usdt_lemon_data.h"

#include "api_client.h"
#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <cmath>
#include <cstring>

namespace {
UsdtFetchStatus mapApi(ApiResult result) {
    switch (result) {
        case API_OK: return USDT_FETCH_OK;
        case API_PARSE_ERROR: return USDT_FETCH_PARSE_ERROR;
        case API_TIMEOUT: return USDT_FETCH_TIMEOUT;
        case API_RATE_LIMITED: return USDT_FETCH_RATE_LIMITED;
        case API_NETWORK_ERROR: return USDT_FETCH_NETWORK_ERROR;
    }
    return USDT_FETCH_NETWORK_ERROR;
}

bool finiteRange(float value, float low, float high) {
    return std::isfinite(value) && value >= low && value <= high;
}

bool equalsIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
        const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

bool parseLemonPrice(const char* json, UsdtPriceData& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    UsdtPriceData next = {};
    next.ask = doc["totalAsk"] | 0.0f;
    next.bid = doc["totalBid"] | 0.0f;
    next.ars = next.ask > 0.0f ? next.ask : next.bid;
    next.valid = finiteRange(next.ask, 100.0f, 100000.0f) &&
                 finiteRange(next.bid, 100.0f, 100000.0f) &&
                 next.ask >= next.bid;
    if (!next.valid) return false;
    next.lastUpdateMs = millis();
    out = next;
    return true;
}

bool parsePeg(const char* simpleJson, const char* marketsJson, UsdtPegData& out) {
    JsonDocument simpleDoc;
    if (deserializeJson(simpleDoc, simpleJson)) return false;
    JsonObject tether = simpleDoc["tether"];
    if (tether.isNull()) return false;
    UsdtPegData next = {};
    next.usd = tether["usd"] | 0.0f;
    next.ars = tether["ars"] | 0.0f;
    next.brl = tether["brl"] | 0.0f;
    next.mxn = tether["mxn"] | 0.0f;
    next.change24h = tether["ars_24h_change"] | 0.0f;
    JsonDocument marketsDoc;
    if (marketsJson && marketsJson[0] && !deserializeJson(marketsDoc, marketsJson) && marketsDoc.size() > 0) {
        JsonObject row = marketsDoc[0];
        next.change1h = row["price_change_percentage_1h_in_currency"] | 0.0f;
        next.change24h = row["price_change_percentage_24h_in_currency"] | next.change24h;
        next.change7d = row["price_change_percentage_7d_in_currency"] | 0.0f;
    }
    next.valid = finiteRange(next.usd, 0.80f, 1.20f) &&
                 finiteRange(next.ars, 100.0f, 100000.0f);
    if (!next.valid) return false;
    next.lastUpdateMs = millis();
    out = next;
    return true;
}

bool parseYield(const char* json, UsdtYieldData& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    JsonArray rows = doc.as<JsonArray>();
    if (rows.isNull()) return false;
    for (JsonObject row : rows) {
        const char* protocol = row["protocol"] | "";
        const char* currency = row["currency"] | "";
        if (!equalsIgnoreCase(protocol, "LEMON_YIELD")) continue;
        if (!equalsIgnoreCase(currency, "USDT") &&
            !equalsIgnoreCase(currency, "USDt")) continue;
        UsdtYieldData next = {};
        const float apr = row["apr"] | 0.0f;
        const float apy = row["apy"] | 0.0f;
        next.aprPercent = apr > 0.0f ? apr : apy * 100.0f;
        next.valid = finiteRange(next.aprPercent, 0.0f, 50.0f);
        if (!next.valid) return false;
        next.lastUpdateMs = millis();
        out = next;
        return true;
    }
    return false;
}
}  // namespace

void usdtDataSetup() {}

bool usdtDataFetch(UsdtDataSnapshot& io) {
    if (WiFi.status() != WL_CONNECTED) return false;
    io.fetching = true;
    bool changed = false;

    ApiResult result = API_NETWORK_ERROR;
    const char* json = apiHttpGet(CRIPTOYA_LEMON_USDT_EP, false, result, 9000, 2048);
    UsdtPriceData lemon = {};
    if (result == API_OK && json && json[0] && parseLemonPrice(json, lemon)) {
        io.lemon = lemon;
        io.lemonStatus = USDT_FETCH_OK;
        changed = true;
    } else {
        io.lemonStatus = result == API_OK ? USDT_FETCH_PARSE_ERROR : mapApi(result);
    }

    result = API_NETWORK_ERROR;
    json = apiHttpGet(COINGECKO_USDT_PEG_EP, true, result, 9000, 4096);
    char simpleBuf[512] = {};
    if (json && json[0]) {
        strncpy(simpleBuf, json, sizeof(simpleBuf) - 1);
    }
    ApiResult marketsResult = API_NETWORK_ERROR;
    const char* marketsJson = apiHttpGet(COINGECKO_USDT_MARKETS_EP, true, marketsResult, 9000, 2048);
    UsdtPegData peg = {};
    if (result == API_OK && simpleBuf[0] && parsePeg(simpleBuf, marketsJson, peg)) {
        io.peg = peg;
        io.pegStatus = USDT_FETCH_OK;
        changed = true;
    } else {
        io.pegStatus = result == API_OK ? USDT_FETCH_PARSE_ERROR : mapApi(result);
    }

    result = API_NETWORK_ERROR;
    json = apiHttpGet(LEMON_YIELD_EP, false, result, 9000, 4096);
    UsdtYieldData yield = {};
    if (result == API_OK && json && json[0] && parseYield(json, yield)) {
        io.yield = yield;
        io.yieldStatus = USDT_FETCH_OK;
        changed = true;
    } else {
        io.yieldStatus = result == API_OK ? USDT_FETCH_PARSE_ERROR : mapApi(result);
    }

    io.fetching = false;
    return changed;
}

void usdtDataUpdateFreshness(UsdtDataSnapshot& io, uint32_t nowMs, bool online) {
    io.online = online;
    io.lemonFreshness = usdtFreshness(io.lemon.valid, io.lemon.lastUpdateMs, nowMs,
                                      online, io.fetching, io.lemonStatus);
    io.pegFreshness = usdtFreshness(io.peg.valid, io.peg.lastUpdateMs, nowMs,
                                    online, io.fetching, io.pegStatus);
    io.yieldFreshness = usdtFreshness(io.yield.valid, io.yield.lastUpdateMs, nowMs,
                                      online, io.fetching, io.yieldStatus);
}

const char* usdtFreshnessLabel(UsdtFreshness freshness) {
    switch (freshness) {
        case USDT_LOADING: return "CARGANDO";
        case USDT_LIVE: return "EN VIVO";
        case USDT_CACHED: return "CACHE";
        case USDT_STALE: return "DESACTUALIZADO";
        case USDT_OFFLINE: return "SIN CONEXION";
        case USDT_RATE_LIMITED: return "LIMITE API";
        case USDT_ERROR: return "ERROR";
    }
    return "ERROR";
}

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

bool parseCoinbaseRates(const char* json, UsdtPegData& out) {
    JsonDocument doc;
    JsonDocument filter;
    filter["data"]["rates"]["USD"] = true;
    filter["data"]["rates"]["BRL"] = true;
    filter["data"]["rates"]["PEN"] = true;
    filter["data"]["rates"]["COP"] = true;
    if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) return false;
    JsonObject rates = doc["data"]["rates"];
    if (rates.isNull()) return false;

    const float usd = rates["USD"].as<float>();
    if (!finiteRange(usd, 0.80f, 1.20f)) return false;

    out.usd = usd;
    out.valid = true;
    out.lastUpdateMs = millis();

    const float brl = rates["BRL"].as<float>();
    const float pen = rates["PEN"].as<float>();
    const float cop = rates["COP"].as<float>();
    out.regionsValid = false;
    if (finiteRange(brl, 1.0f, 20.0f) &&
        finiteRange(pen, 1.0f, 20.0f) &&
        finiteRange(cop, 100.0f, 20000.0f)) {
        out.brl = brl;
        out.pen = pen;
        out.cop = cop;
        out.regionsValid = true;
        out.regionsLastUpdateMs = out.lastUpdateMs;
    }
    return true;
}

bool parseMarketChart(const char* json, UsdtPegData& out) {
    JsonDocument doc;
    JsonDocument filter;
    filter["prices"] = true;
    if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) return false;
    JsonArray prices = doc["prices"];
    if (prices.size() < 2) return false;

    JsonArray latest = prices[prices.size() - 1];
    const uint64_t latestMs = latest[0].as<uint64_t>();
    const float latestPrice = latest[1].as<float>();
    if (latestMs == 0 || !finiteRange(latestPrice, 100.0f, 100000.0f)) return false;

    auto closestPrice = [&](uint64_t targetMs) {
        float bestPrice = 0.0f;
        uint64_t bestDelta = UINT64_MAX;
        for (JsonArray point : prices) {
            const uint64_t timestamp = point[0].as<uint64_t>();
            const float price = point[1].as<float>();
            if (timestamp == 0 || !finiteRange(price, 100.0f, 100000.0f)) continue;
            const uint64_t delta = timestamp > targetMs ? timestamp - targetMs
                                                        : targetMs - timestamp;
            if (delta < bestDelta) {
                bestDelta = delta;
                bestPrice = price;
            }
        }
        return bestPrice;
    };

    constexpr uint64_t HOUR_MS = 60ULL * 60ULL * 1000ULL;
    const float price1h = closestPrice(latestMs - HOUR_MS);
    const float price24h = closestPrice(latestMs - 24ULL * HOUR_MS);
    const float price7d = closestPrice(latestMs - 7ULL * 24ULL * HOUR_MS);
    if (price1h <= 0.0f || price24h <= 0.0f || price7d <= 0.0f) return false;

    const float change1h = (latestPrice / price1h - 1.0f) * 100.0f;
    const float change24h = (latestPrice / price24h - 1.0f) * 100.0f;
    const float change7d = (latestPrice / price7d - 1.0f) * 100.0f;
    if (!finiteRange(change1h, -100.0f, 100.0f) ||
        !finiteRange(change24h, -100.0f, 100.0f) ||
        !finiteRange(change7d, -100.0f, 100.0f)) return false;

    out.ars = latestPrice;
    out.change1h = change1h;
    out.change24h = change24h;
    out.change7d = change7d;
    out.variationsValid = true;
    out.variationsLastUpdateMs = millis();
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

bool parseNetworkSupply(const char* json, UsdtNetworkData& out) {
    static const char* networkIds[USDT_NETWORK_COUNT] = {
        "bsc", "polygon", "tron", "ethereum"
    };
    JsonDocument filter;
    filter["networks"][0]["id"] = true;
    filter["networks"][0]["supplyUsd"] = true;
    filter["networks"][0]["change24h"] = true;

    JsonDocument doc;
    if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) {
        return false;
    }
    JsonArray rows = doc["networks"];
    if (rows.size() != USDT_NETWORK_COUNT) return false;

    UsdtNetworkData next = {};
    for (JsonObject row : rows) {
        const char* id = row["id"] | "";
        int index = -1;
        for (uint8_t i = 0; i < USDT_NETWORK_COUNT; ++i) {
            if (equalsIgnoreCase(id, networkIds[i])) index = i;
        }
        if (index < 0 || next.metrics[index].valid) return false;
        const float supply = row["supplyUsd"] | 0.0f;
        const float change = row["change24h"] | NAN;
        if (!finiteRange(supply, 1.0f, 1.0e12f) ||
            !finiteRange(change, -100.0f, 100.0f)) return false;
        next.metrics[index].supplyUsd = supply;
        next.metrics[index].change24h = change;
        next.metrics[index].valid = true;
    }
    for (uint8_t i = 0; i < USDT_NETWORK_COUNT; ++i) {
        if (!next.metrics[i].valid) return false;
    }
    next.valid = true;
    next.lastUpdateMs = millis();
    next.lastAttemptMs = next.lastUpdateMs;
    out = next;
    return true;
}
}  // namespace

void usdtDataSetup() {}

bool usdtDataFetchPrice(UsdtDataSnapshot& io) {
    if (WiFi.status() != WL_CONNECTED) return false;
    ApiResult result = API_NETWORK_ERROR;
    const char* json = apiHttpGet(
        CRIPTOYA_LEMON_USDT_EP, false, result, 5000, 2048, 1);
    UsdtPriceData lemon = {};
    if (result == API_OK && json && json[0] && parseLemonPrice(json, lemon)) {
        io.lemon = lemon;
        io.lemonStatus = USDT_FETCH_OK;
        return true;
    } else {
        io.lemonStatus = result == API_OK ? USDT_FETCH_PARSE_ERROR : mapApi(result);
    }
    return false;
}

bool usdtDataFetchRates(UsdtDataSnapshot& io) {
    if (WiFi.status() != WL_CONNECTED) return false;
    ApiResult result = API_NETWORK_ERROR;
    const char* json = apiHttpGet(
        COINBASE_USDT_RATES_EP, false, result, 5000, 24576, 1);
    if (result == API_OK && json && json[0] && parseCoinbaseRates(json, io.peg)) {
        io.pegStatus = USDT_FETCH_OK;
        return true;
    } else {
        io.pegStatus = result == API_OK ? USDT_FETCH_PARSE_ERROR : mapApi(result);
    }
    return false;
}

bool usdtDataFetchVariations(UsdtDataSnapshot& io) {
    if (WiFi.status() != WL_CONNECTED) return false;
    const uint32_t nowMs = millis();
    const uint32_t interval = io.peg.variationsValid ? USDT_VARIATIONS_REFRESH_MS
                                                     : USDT_VARIATIONS_RETRY_MS;
    const bool chartDue = io.peg.variationsLastAttemptMs == 0 ||
        nowMs - io.peg.variationsLastAttemptMs >= interval;
    if (!chartDue) return false;

    io.peg.variationsLastAttemptMs = nowMs;
    ApiResult result = API_NETWORK_ERROR;
    const char* json = apiHttpGet(
        COINGECKO_USDT_CHART_EP, true, result, 5000, 24576, 1);
    if (result == API_OK && json && json[0] && parseMarketChart(json, io.peg)) {
        io.variationsStatus = USDT_FETCH_OK;
        return true;
    }
    io.variationsStatus = result == API_OK ? USDT_FETCH_PARSE_ERROR : mapApi(result);
    return false;
}

bool usdtDataFetchYield(UsdtDataSnapshot& io) {
    if (WiFi.status() != WL_CONNECTED) return false;
    ApiResult result = API_NETWORK_ERROR;
    const char* json = apiHttpGet(
        LEMON_YIELD_EP, false, result, 5000, 4096, 1);
    UsdtYieldData yield = {};
    if (result == API_OK && json && json[0] && parseYield(json, yield)) {
        io.yield = yield;
        io.yieldStatus = USDT_FETCH_OK;
        return true;
    } else {
        io.yieldStatus = result == API_OK ? USDT_FETCH_PARSE_ERROR : mapApi(result);
    }
    return false;
}

bool usdtDataFetchNetworks(UsdtDataSnapshot& io) {
    if (WiFi.status() != WL_CONNECTED) return false;
    const uint32_t nowMs = millis();
    const uint32_t interval = io.networks.valid ? USDT_NETWORKS_REFRESH_MS
                                                : USDT_NETWORKS_RETRY_MS;
    const bool due = io.networks.lastAttemptMs == 0 ||
        nowMs - io.networks.lastAttemptMs >= interval;
    if (!due) return false;

    io.networks.lastAttemptMs = nowMs;
    ApiResult result = API_NETWORK_ERROR;
    const char* json = apiHttpGet(
        LEMON_USDT_NETWORKS_EP, false, result, 5000, 2048, 1);
    UsdtNetworkData networks = {};
    if (result == API_OK && json && json[0] &&
        parseNetworkSupply(json, networks)) {
        io.networks = networks;
        io.networksStatus = USDT_FETCH_OK;
        return true;
    }
    io.networksStatus = result == API_OK ? USDT_FETCH_PARSE_ERROR : mapApi(result);
    return false;
}

void usdtDataUpdateFreshness(UsdtDataSnapshot& io, uint32_t nowMs, bool online) {
    io.online = online;
    io.lemonFreshness = usdtFreshness(io.lemon.valid, io.lemon.lastUpdateMs, nowMs,
                                      online, io.fetching, io.lemonStatus);
    io.pegFreshness = usdtFreshness(io.peg.valid, io.peg.lastUpdateMs, nowMs,
                                    online, io.fetching, io.pegStatus);
    io.yieldFreshness = usdtFreshness(io.yield.valid, io.yield.lastUpdateMs, nowMs,
                                      online, io.fetching, io.yieldStatus);
    io.networksFreshness = usdtFreshness(
        io.networks.valid, io.networks.lastUpdateMs, nowMs,
        online, io.fetching, io.networksStatus);
}

const char* usdtFreshnessLabel(UsdtFreshness freshness) {
    switch (freshness) {
        case USDT_LOADING: return "CARGANDO";
        case USDT_LIVE: return "EN VIVO";
        case USDT_CACHED: return "CACHE";
        case USDT_STALE: return "DESACTUALIZADO";
        case USDT_OFFLINE: return "SIN CONEXION";
        case USDT_RATE_LIMITED: return "LIMITE API";
        case USDT_ERROR: return "REINTENTO";
    }
    return "REINTENTO";
}

const char* usdtFetchStatusLabel(UsdtFetchStatus status) {
    switch (status) {
        case USDT_FETCH_OK: return "OK";
        case USDT_FETCH_NETWORK_ERROR: return "RED";
        case USDT_FETCH_PARSE_ERROR: return "FORMATO";
        case USDT_FETCH_TIMEOUT: return "TIMEOUT";
        case USDT_FETCH_RATE_LIMITED: return "LIMITE";
    }
    return "RED";
}

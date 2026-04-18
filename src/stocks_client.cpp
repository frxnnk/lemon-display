#include "stocks_client.h"
#include "api_client.h"
#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>
#include <cmath>

// ── Yahoo Finance v7 quote: /v7/finance/quote?symbols=AAPL,TSLA,NVDA ──
ApiResult fetchStockQuotes(const char* csvSymbols, StockQuote* out, int maxOut, int& outCount) {
    outCount = 0;
    if (!csvSymbols || !out || maxOut <= 0) return API_PARSE_ERROR;

    static char urlBuf[512];
    int n = snprintf(urlBuf, sizeof(urlBuf), "%s%s", YAHOO_QUOTE_EP, csvSymbols);
    if (n <= 0 || n >= (int)sizeof(urlBuf)) return API_NETWORK_ERROR;

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result, 10000);
    if (result != API_OK) return result;
    if (!json || !json[0]) return API_NETWORK_ERROR;

    // Filter: only the fields we actually render.
    JsonDocument filter;
    filter["quoteResponse"]["result"][0]["symbol"]                      = true;
    filter["quoteResponse"]["result"][0]["shortName"]                   = true;
    filter["quoteResponse"]["result"][0]["longName"]                    = true;
    filter["quoteResponse"]["result"][0]["regularMarketPrice"]          = true;
    filter["quoteResponse"]["result"][0]["regularMarketChange"]         = true;
    filter["quoteResponse"]["result"][0]["regularMarketChangePercent"]  = true;
    filter["quoteResponse"]["result"][0]["regularMarketDayHigh"]        = true;
    filter["quoteResponse"]["result"][0]["regularMarketDayLow"]         = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(15));
    if (err) {
        Serial.printf("[Yahoo] quote JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray arr = doc["quoteResponse"]["result"];
    if (arr.isNull()) return API_PARSE_ERROR;

    for (JsonObject obj : arr) {
        if (outCount >= maxOut) break;
        StockQuote& q = out[outCount];
        memset(&q, 0, sizeof(q));
        const char* sym  = obj["symbol"]    | (const char*)nullptr;
        const char* nm   = obj["shortName"] | obj["longName"] | "";
        if (!sym) continue;
        strncpy(q.symbol, sym, STOCK_SYMBOL_LEN - 1);
        strncpy(q.name,   nm,  STOCK_NAME_LEN - 1);
        q.price      = obj["regularMarketPrice"]         | NAN;
        q.change     = obj["regularMarketChange"]        | 0.0f;
        q.changePct  = obj["regularMarketChangePercent"] | 0.0f;
        q.dayHigh    = obj["regularMarketDayHigh"]       | NAN;
        q.dayLow     = obj["regularMarketDayLow"]        | NAN;
        q.valid      = isfinite(q.price) && q.price > 0.0f;
        q.lastUpdate = millis();
        outCount++;
    }

    Serial.printf("[Yahoo] Got %d quotes\n", outCount);
    return (outCount > 0) ? API_OK : API_PARSE_ERROR;
}

// ── Yahoo Finance v8 chart: /v8/finance/chart/AAPL?range=...&interval=... ──
ApiResult fetchStockChart(const char* symbol, const char* range, const char* interval,
                          SparklineData& out) {
    out.valid = false;
    out.count = 0;
    if (!symbol || !range || !interval) return API_PARSE_ERROR;

    static char urlBuf[256];
    int n = snprintf(urlBuf, sizeof(urlBuf),
                     "%s%s?range=%s&interval=%s",
                     YAHOO_CHART_EP, symbol, range, interval);
    if (n <= 0 || n >= (int)sizeof(urlBuf)) return API_NETWORK_ERROR;

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result, 12000);
    if (result != API_OK) return result;

    JsonDocument filter;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(20));
    if (err) {
        Serial.printf("[Yahoo] chart JSON error (%s): %s\n", symbol, err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray closes = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"];
    if (closes.isNull() || closes.size() == 0) return API_PARSE_ERROR;

    int total = (int)closes.size();
    int targetCount = (total <= SPARKLINE_POINTS) ? total : SPARKLINE_POINTS;
    float step = (float)total / (float)targetCount;

    float minV = 1e12f, maxV = -1e12f;
    float lastGood = NAN;
    int written = 0;
    for (int i = 0; i < targetCount; i++) {
        int idx = (int)(i * step);
        if (idx >= total) idx = total - 1;
        float v = closes[idx] | NAN;
        if (!isfinite(v) || v <= 0.0f) {
            // Yahoo sometimes returns null for pre-market / post-close bars —
            // carry last good forward rather than drop the point.
            if (isfinite(lastGood)) v = lastGood;
            else continue;
        }
        out.points[written++] = v;
        lastGood = v;
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
    }

    if (written < 2 || !(maxV > 0.0f)) return API_PARSE_ERROR;
    out.count = (uint16_t)written;
    out.minVal = minV;
    out.maxVal = maxV;
    out.valid  = true;
    out.lastUpdate = millis();
    Serial.printf("[Yahoo] %s chart (%s/%s): %d pts, $%.2f-$%.2f\n",
                  symbol, range, interval, out.count, out.minVal, out.maxVal);
    return API_OK;
}

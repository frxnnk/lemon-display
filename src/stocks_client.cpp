#include "stocks_client.h"
#include "api_client.h"
#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>
#include <cmath>

// ── Yahoo Finance v8 chart: /v8/finance/chart/SYM?range=...&interval=... ──
// Single request returns both quote-like meta + the close[] array we
// downsample into a SparklineData. Avoids the /v7/finance/quote endpoint
// which now requires auth (401 without crumb/cookie).
ApiResult fetchStockChart(const char* symbol, const char* range, const char* interval,
                          StockQuote& quote, SparklineData& spark) {
    memset(&quote, 0, sizeof(quote));
    spark.valid = false;
    spark.count = 0;
    if (!symbol || !range || !interval) return API_PARSE_ERROR;

    static char urlBuf[256];
    int n = snprintf(urlBuf, sizeof(urlBuf),
                     "%s%s?range=%s&interval=%s",
                     YAHOO_CHART_EP, symbol, range, interval);
    if (n <= 0 || n >= (int)sizeof(urlBuf)) return API_NETWORK_ERROR;

    ApiResult result;
    // 5s cap on Yahoo so a slow symbol can't freeze the UI for the full 8s.
    const char* json = apiHttpGet(urlBuf, false, result, 5000);
    if (result != API_OK) return result;
    if (!json || !json[0]) return API_NETWORK_ERROR;

    // Filter: only the fields we actually render.
    JsonDocument filter;
    filter["chart"]["result"][0]["meta"]["symbol"]                   = true;
    filter["chart"]["result"][0]["meta"]["shortName"]                = true;
    filter["chart"]["result"][0]["meta"]["longName"]                 = true;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"]       = true;
    filter["chart"]["result"][0]["meta"]["previousClose"]            = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"]       = true;
    filter["chart"]["result"][0]["meta"]["regularMarketDayHigh"]     = true;
    filter["chart"]["result"][0]["meta"]["regularMarketDayLow"]      = true;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["close"]  = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(20));
    if (err) {
        Serial.printf("[Yahoo] chart JSON error (%s): %s\n", symbol, err.c_str());
        return API_PARSE_ERROR;
    }

    JsonObject meta = doc["chart"]["result"][0]["meta"];
    if (meta.isNull()) {
        Serial.printf("[Yahoo] chart result missing for %s\n", symbol);
        return API_PARSE_ERROR;
    }

    // ── Quote ──
    const char* sym  = meta["symbol"]    | symbol;
    const char* nm   = meta["shortName"] | meta["longName"] | "";
    strncpy(quote.symbol, sym, STOCK_SYMBOL_LEN - 1);
    strncpy(quote.name,   nm,  STOCK_NAME_LEN - 1);
    quote.price     = meta["regularMarketPrice"]     | NAN;
    float prevClose = meta["previousClose"]          | (meta["chartPreviousClose"] | NAN);
    quote.dayHigh   = meta["regularMarketDayHigh"]   | NAN;
    quote.dayLow    = meta["regularMarketDayLow"]    | NAN;
    if (isfinite(quote.price) && isfinite(prevClose) && prevClose > 0.0f) {
        quote.change    = quote.price - prevClose;
        quote.changePct = (quote.change / prevClose) * 100.0f;
    } else {
        quote.change = 0.0f;
        quote.changePct = 0.0f;
    }
    quote.valid      = isfinite(quote.price) && quote.price > 0.0f;
    quote.lastUpdate = millis();

    // ── Sparkline ──
    JsonArray closes = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"];
    if (closes.isNull() || closes.size() == 0) {
        Serial.printf("[Yahoo] %s: quote OK, chart empty\n", symbol);
        return quote.valid ? API_OK : API_PARSE_ERROR;
    }

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
            if (isfinite(lastGood)) v = lastGood;
            else continue;
        }
        spark.points[written++] = v;
        lastGood = v;
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
    }

    if (written >= 2 && maxV > 0.0f) {
        spark.count  = (uint16_t)written;
        spark.minVal = minV;
        spark.maxVal = maxV;
        spark.valid  = true;
        spark.lastUpdate = millis();
        Serial.printf("[Yahoo] %s chart (%s/%s): $%.2f %+.2f (%.2f%%)  %d spark pts\n",
                      quote.symbol, range, interval, quote.price,
                      quote.change, quote.changePct, spark.count);
    } else {
        Serial.printf("[Yahoo] %s quote OK but spark invalid\n", quote.symbol);
    }

    return quote.valid ? API_OK : API_PARSE_ERROR;
}

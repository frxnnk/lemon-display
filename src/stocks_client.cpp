#include "stocks_client.h"
#include "api_client.h"
#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include <cstring>
#include <cmath>

// ── Dedicated HTTP pipeline (off api_client's shared one) ──
// Stocks fetch runs on a FreeRTOS worker on core 0; api_client's statics
// are used concurrently by scheduler tasks on core 1, so we need our own
// set of statics. Buffer lives in PSRAM (Yahoo chart is ~30KB typical).
static WiFiClientSecure _stkClient;
static HTTPClient       _stkHttp;
static char*            _stkBuf = nullptr;
static const size_t     STK_BUF_SIZE = 64 * 1024;

static const char* stocksHttpGet(const char* url, ApiResult& result, int timeoutMs) {
    result = API_NETWORK_ERROR;
    if (!_stkBuf) {
        _stkBuf = (char*)ps_malloc(STK_BUF_SIZE);
        if (!_stkBuf) {
            Serial.println("[Stocks] FATAL: cannot allocate response buffer");
            return "";
        }
    }
    _stkBuf[0] = '\0';

    _stkClient.setInsecure();
    _stkHttp.setConnectTimeout(5000);
    _stkHttp.setTimeout(timeoutMs);
    if (!_stkHttp.begin(_stkClient, url)) {
        _stkClient.stop();
        return "";
    }
    _stkHttp.addHeader("Accept", "application/json");
    _stkHttp.setUserAgent("Mozilla/5.0 (compatible; Lemon-Box/5.0)");

    esp_task_wdt_reset();
    int code = _stkHttp.GET();
    esp_task_wdt_reset();
    if (code != 200) {
        Serial.printf("[Stocks] HTTP %d %s\n", code, url);
        _stkHttp.end();
        _stkClient.stop();
        return "";
    }

    int contentLen = _stkHttp.getSize();
    int bytesRead = 0;
    if (contentLen > 0 && contentLen < (int)(STK_BUF_SIZE - 1)) {
        WiFiClient* stream = _stkHttp.getStreamPtr();
        unsigned long readStart = millis();
        while (bytesRead < contentLen) {
            if ((int)(millis() - readStart) > timeoutMs) {
                Serial.printf("[Stocks] body read timeout (%d/%d)\n", bytesRead, contentLen);
                break;
            }
            int avail = stream->available();
            if (avail > 0) {
                int toRead = avail;
                if (toRead > contentLen - bytesRead) toRead = contentLen - bytesRead;
                int n = stream->readBytes(_stkBuf + bytesRead, toRead);
                if (n <= 0) break;
                bytesRead += n;
                esp_task_wdt_reset();
            } else if (!stream->connected()) {
                break;
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
                esp_task_wdt_reset();
            }
        }
    }
    _stkBuf[bytesRead] = '\0';

    _stkHttp.end();
    _stkClient.stop();

    if (bytesRead > 0) result = API_OK;
    return _stkBuf;
}

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
    // Uses the stocks-dedicated HTTP pipeline (see stocksHttpGet above) so
    // the FreeRTOS worker can fetch without racing api_client's shared
    // secureClient that runs on the main-loop scheduler.
    const char* json = stocksHttpGet(urlBuf, result, 5000);
    if (result != API_OK) return result;
    if (!json || !json[0]) return API_NETWORK_ERROR;

    // Filter: only the fields we actually render. "indicators" is pulled in
    // whole because ArduinoJson's filter semantics on nested arrays
    // (result[0].indicators.quote[0].close) dropped the close series on
    // some Yahoo responses, leaving the sparkline permanently invalid.
    // Including the full indicators subtree adds a few KB of parse work but
    // guarantees close[] lands in the doc.
    JsonDocument filter;
    filter["chart"]["result"][0]["meta"]["symbol"]                   = true;
    filter["chart"]["result"][0]["meta"]["shortName"]                = true;
    filter["chart"]["result"][0]["meta"]["longName"]                 = true;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"]       = true;
    filter["chart"]["result"][0]["meta"]["previousClose"]            = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"]       = true;
    filter["chart"]["result"][0]["meta"]["regularMarketDayHigh"]     = true;
    filter["chart"]["result"][0]["meta"]["regularMarketDayLow"]      = true;
    filter["chart"]["result"][0]["indicators"]                       = true;

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

#include "api_client.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <cctype>
#include <cmath>
#include <cstring>

static WiFiClientSecure secureClient;

void apiSetup() {
    secureClient.setInsecure(); // Skip cert validation (ESP32 has limited CA store)
}

// ── Helper: perform HTTPS GET with 1 retry ──
static String httpGet(const char* url, bool addCoinGeckoKey, ApiResult& result, int timeoutMs = 10000) {
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) {
            Serial.printf("[API] Retry %d for %s\n", attempt, url);
            delay(2000);
        }

        HTTPClient http;
        http.setConnectTimeout(5000);
        http.setTimeout(timeoutMs);

        String fullUrl = String(url);
        if (addCoinGeckoKey) {
            fullUrl += (fullUrl.indexOf('?') >= 0) ? "&" : "?";
            fullUrl += "x_cg_demo_api_key=";
            fullUrl += COINGECKO_API_KEY;
        }

        if (!http.begin(secureClient, fullUrl)) {
            Serial.printf("[API] Failed to begin: %s\n", url);
            result = API_NETWORK_ERROR;
            http.end();
            continue;
        }
        http.addHeader("Accept", "application/json");

        int code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            http.end();
            result = API_OK;
            return payload;
        } else if (code == -1 || code == -11) {
            Serial.printf("[API] Timeout (code %d) from %s\n", code, url);
            result = API_TIMEOUT;
        } else {
            Serial.printf("[API] HTTP %d from %s\n", code, url);
            result = API_NETWORK_ERROR;
        }
        http.end();
    }
    return "";
}

// ── CoinGecko: BTC price + 1h/24h/7d changes ──
ApiResult fetchBtcPrice(BtcPrice& out) {
    ApiResult result;
    String url = "https://api.coingecko.com/api/v3/coins/bitcoin?localization=false&tickers=false&community_data=false&developer_data=false&sparkline=false";
    String json = httpGet(url.c_str(), false, result);
    if (json.isEmpty()) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, DeserializationOption::NestingLimit(15));
    if (err) {
        Serial.printf("[API] BTC JSON parse error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonObject md = doc["market_data"];
    out.usd = md["current_price"]["usd"] | 0.0f;
    out.change1h = md["price_change_percentage_1h_in_currency"]["usd"] | 0.0f;
    out.change24h = md["price_change_percentage_24h"] | 0.0f;
    out.change7d = md["price_change_percentage_7d"] | 0.0f;
    out.ath = md["ath"]["usd"] | 0.0f;
    out.athChangePercent = md["ath_change_percentage"]["usd"] | 0.0f;
    out.valid = (out.usd > 0);
    out.lastUpdate = millis();

    Serial.printf("[API] BTC: $%.0f (1h:%.2f%% 24h:%.2f%% 7d:%.2f%% ATH:$%.0f)\n",
                  out.usd, out.change1h, out.change24h, out.change7d, out.ath);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── CoinGecko: Lightweight BTC price (for real-time mode) ──
ApiResult fetchBtcPriceSimple(BtcPrice& out) {
    ApiResult result;
    String json = httpGet(COINGECKO_SIMPLE_EP, false, result);
    if (json.isEmpty()) return result;

    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        Serial.println("[API] Simple BTC JSON parse error");
        return API_PARSE_ERROR;
    }

    JsonObject btc = doc["bitcoin"];
    float price = btc["usd"] | 0.0f;
    if (price <= 0) return API_PARSE_ERROR;

    out.usd = price;
    out.change24h = btc["usd_24h_change"] | out.change24h;
    out.valid = true;
    out.lastUpdate = millis();

    Serial.printf("[API] BTC simple: $%.0f\n", out.usd);
    return API_OK;
}

// ── CoinGecko: Global market data ──
ApiResult fetchGlobalData(CryptoGlobal& out) {
    ApiResult result;
    String json = httpGet(COINGECKO_GLOBAL_EP, false, result);
    if (json.isEmpty()) return result;

    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        Serial.println("[API] Global JSON parse error");
        return API_PARSE_ERROR;
    }

    JsonObject data = doc["data"];
    out.btcDominance = data["market_cap_percentage"]["btc"] | 0.0f;
    out.ethDominance = data["market_cap_percentage"]["eth"] | 0.0f;
    out.totalMarketCapChangePercent24h = data["market_cap_change_percentage_24h_usd"] | 0.0f;
    out.totalVolumeChangePercent24h = 0.0f;
    out.valid = (out.btcDominance > 0);
    out.lastUpdate = millis();

    Serial.printf("[API] Global: BTC dom=%.1f%% ETH dom=%.1f%% MCap chg=%.2f%%\n",
                  out.btcDominance, out.ethDominance, out.totalMarketCapChangePercent24h);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── CoinGecko: Sparkline (downsample to SPARKLINE_POINTS) ──
ApiResult fetchSparkline(SparklineData& out, int days, CoinId coin) {
    const char* geckoId = "bitcoin";
    switch (coin) {
        case COIN_ETH: geckoId = "ethereum"; break;
        case COIN_SOL: geckoId = "solana";   break;
        default:       geckoId = "bitcoin";  break;
    }

    char urlBuf[256];
    snprintf(urlBuf, sizeof(urlBuf), COINGECKO_CHART_EP_FMT "%d", geckoId, days);

    // Use the shared httpGet helper (reads full response as String)
    ApiResult result;
    String json = httpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    // Parse JSON from String with filter (only keep "prices")
    JsonDocument filter;
    filter["prices"][0][0] = true;
    filter["prices"][0][1] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(15));

    if (err) {
        Serial.printf("[API] Sparkline JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray prices = doc["prices"];
    int total = prices.size();
    Serial.printf("[API] Sparkline parsed: %d points\n", total);
    if (total == 0) return API_PARSE_ERROR;

    int targetCount = min((int)SPARKLINE_POINTS, total);
    out.count = targetCount;
    out.minVal = 1e12;
    out.maxVal = -1e12;

    float step = (float)total / targetCount;
    for (int i = 0; i < targetCount; i++) {
        int idx = (int)(i * step);
        if (idx >= total) idx = total - 1;
        float val = prices[idx][1].as<float>();
        out.points[i] = val;
        if (val < out.minVal) out.minVal = val;
        if (val > out.maxVal) out.maxVal = val;
    }

    out.valid = true;
    out.lastUpdate = millis();
    Serial.printf("[API] Sparkline %s (%dd): %d pts, $%.0f-$%.0f\n",
                  geckoId, days, out.count, out.minVal, out.maxVal);
    return API_OK;
}

// ── CoinGecko: Multi-coin market data (single request for all coins) ──
ApiResult fetchMarketData(MarketData& out) {
    ApiResult result;
    String json = httpGet(COINGECKO_MARKETS_EP, false, result);
    if (json.isEmpty()) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, DeserializationOption::NestingLimit(15));
    if (err) {
        Serial.printf("[API] Markets JSON parse error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    // Map CoinGecko IDs to our CoinId enum
    struct IdMap { const char* geckoId; CoinId coinId; const char* symbol; };
    static const IdMap idMap[] = {
        { "bitcoin",  COIN_BTC,  "BTC"  },
        { "ethereum", COIN_ETH,  "ETH"  },
        { "solana",   COIN_SOL,  "SOL"  },
        { "tether",   COIN_USDT, "USDT" },
        { "usd-coin", COIN_USDC, "USDC" },
    };

    // Clear validity
    for (int i = 0; i < COIN_COUNT; i++) {
        out.coins[i].valid = false;
    }

    for (JsonObject coin : arr) {
        const char* id = coin["id"] | "";
        for (const auto& m : idMap) {
            if (strcmp(id, m.geckoId) == 0) {
                CoinData& c = out.coins[m.coinId];
                strncpy(c.symbol, m.symbol, sizeof(c.symbol) - 1);
                c.symbol[sizeof(c.symbol) - 1] = '\0';
                c.priceUsd  = coin["current_price"] | 0.0f;
                c.change1h  = coin["price_change_percentage_1h_in_currency"] | 0.0f;
                c.change24h = coin["price_change_percentage_24h_in_currency"] | 0.0f;
                c.change7d  = coin["price_change_percentage_7d_in_currency"] | 0.0f;
                c.marketCap = coin["market_cap"] | 0.0f;
                c.valid     = (c.priceUsd > 0);

                Serial.printf("[API] %s: $%.2f (1h:%.2f%% 24h:%.2f%%)\n",
                              c.symbol, c.priceUsd, c.change1h, c.change24h);
                break;
            }
        }
    }

    out.lastUpdate = millis();

    // Consider success if at least BTC parsed
    return out.coins[COIN_BTC].valid ? API_OK : API_PARSE_ERROR;
}

// ── Binance: Kline data for sparkline (24h, 7d views) ──
ApiResult fetchBinanceKlines(SparklineData& out, const char* interval, int limit) {
    char urlBuf[128];
    snprintf(urlBuf, sizeof(urlBuf),
             "%s?symbol=BTCUSDT&interval=%s&limit=%d",
             BINANCE_KLINES_EP, interval, limit);

    ApiResult result;
    String json = httpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[API] Binance klines JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    int total = arr.size();
    int targetCount = (total <= SPARKLINE_POINTS) ? total : SPARKLINE_POINTS;

    out.count = targetCount;
    out.minVal = 1e12f;
    out.maxVal = -1e12f;

    float step = (float)total / targetCount;
    int i = 0;
    for (int t = 0; t < targetCount; t++) {
        int idx = (int)(t * step);
        if (idx >= total) idx = total - 1;
        JsonArray kline = arr[idx];
        const char* closeStr = kline[4].as<const char*>();
        float val = closeStr ? atof(closeStr) : 0.0f;
        if (val <= 0) continue;
        out.points[i] = val;
        if (val < out.minVal) out.minVal = val;
        if (val > out.maxVal) out.maxVal = val;
        i++;
    }
    out.count = i;

    out.valid = (i >= 2);
    out.lastUpdate = millis();

    Serial.printf("[API] Binance klines (%s, %d): %d pts, $%.0f-$%.0f\n",
                  interval, limit, out.count, out.minVal, out.maxVal);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── Binance: OHLC candlestick data ──
ApiResult fetchBinanceOhlc(OhlcData& out, const char* interval, int limit) {
    char urlBuf[128];
    snprintf(urlBuf, sizeof(urlBuf),
             "%s?symbol=BTCUSDT&interval=%s&limit=%d",
             BINANCE_KLINES_EP, interval, limit);

    ApiResult result;
    String json = httpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[API] Binance OHLC JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    int total = arr.size();
    int count = (total <= OHLC_MAX_BARS) ? total : OHLC_MAX_BARS;

    out.count = 0;
    out.minVal = 1e12f;
    out.maxVal = -1e12f;

    for (int i = 0; i < count; i++) {
        JsonArray kline = arr[i];
        // Binance kline indices: 1=open, 2=high, 3=low, 4=close
        const char* openStr  = kline[1].as<const char*>();
        const char* highStr  = kline[2].as<const char*>();
        const char* lowStr   = kline[3].as<const char*>();
        const char* closeStr = kline[4].as<const char*>();

        float o = openStr  ? atof(openStr)  : 0.0f;
        float h = highStr  ? atof(highStr)  : 0.0f;
        float l = lowStr   ? atof(lowStr)   : 0.0f;
        float c = closeStr ? atof(closeStr) : 0.0f;

        if (o <= 0 || h <= 0 || l <= 0 || c <= 0) continue;

        out.bars[out.count] = { o, h, l, c };
        if (l < out.minVal) out.minVal = l;
        if (h > out.maxVal) out.maxVal = h;
        out.count++;
    }

    out.valid = (out.count >= 2);
    out.lastUpdate = millis();

    Serial.printf("[API] Binance OHLC (%s, %d): %d bars, $%.0f-$%.0f\n",
                  interval, limit, out.count, out.minVal, out.maxVal);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── CoinGecko: USDC/ARS sparkline (for dollar chart) ──
ApiResult fetchLemonSparkline(SparklineData& out, int days) {
    // precision=2 reduces response size ~40% (ARS prices don't need 15 decimals)
    char urlBuf[160];
    snprintf(urlBuf, sizeof(urlBuf), "%s%d&precision=2", COINGECKO_TETHER_CHART_EP, days);

    // Longer timeout for large periods (90d+ = hourly data, big response)
    int timeout = (days > 30) ? 15000 : 10000;
    ApiResult result;
    String json = httpGet(urlBuf, false, result, timeout);
    if (result != API_OK) return result;

    Serial.printf("[API] Lemon sparkline response: %d bytes\n", json.length());

    JsonDocument filter;
    filter["prices"][0][0] = true;
    filter["prices"][0][1] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(15));
    json = String();  // free memory immediately after parse

    if (err) {
        Serial.printf("[API] Lemon sparkline JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray prices = doc["prices"];
    int total = prices.size();
    Serial.printf("[API] Lemon sparkline parsed: %d points for %dd\n", total, days);
    if (total == 0) return API_PARSE_ERROR;

    // Build into temp to avoid corrupting `out` on bad data
    // For longer periods (30d+), cap points for smoother chart (hourly data gets noisy)
    int maxTarget = (days >= 30) ? 180 : (int)SPARKLINE_POINTS;
    int targetCount = min(maxTarget, total);
    float tempMin = 1e12f, tempMax = -1e12f;
    float step = (float)total / targetCount;
    float lastGood = 0;
    static float tempPoints[SPARKLINE_POINTS];

    for (int i = 0; i < targetCount; i++) {
        int idx = (int)(i * step);
        if (idx >= total) idx = total - 1;
        float val = prices[idx][1].as<float>();
        if (!isfinite(val) || val <= 0) val = lastGood;
        tempPoints[i] = val;
        lastGood = val;
        if (val > 0 && val < tempMin) tempMin = val;
        if (val > 0 && val > tempMax) tempMax = val;
    }

    if (tempMax <= 0) {
        Serial.printf("[API] Lemon sparkline %dd: all data invalid\n", days);
        return API_PARSE_ERROR;
    }

    // Data is valid — commit to output
    out.count = targetCount;
    out.minVal = tempMin;
    out.maxVal = tempMax;
    memcpy(out.points, tempPoints, targetCount * sizeof(float));
    out.valid = true;
    out.lastUpdate = millis();
    Serial.printf("[API] Lemon sparkline (%dd): %d pts, $%.0f-$%.0f\n",
                  days, out.count, out.minVal, out.maxVal);
    return API_OK;
}

// ── Binance: Kline data with parameterized symbol (for pair switching) ──
ApiResult fetchBinanceKlinesSymbol(SparklineData& out, const char* symbol,
                                    const char* interval, int limit, bool invert) {
    char urlBuf[128];
    snprintf(urlBuf, sizeof(urlBuf),
             "%s?symbol=%s&interval=%s&limit=%d",
             BINANCE_KLINES_EP, symbol, interval, limit);

    ApiResult result;
    String json = httpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[API] Binance klines JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    int total = arr.size();
    int targetCount = (total <= SPARKLINE_POINTS) ? total : SPARKLINE_POINTS;

    out.count = targetCount;
    out.minVal = 1e12f;
    out.maxVal = -1e12f;

    float step = (float)total / targetCount;
    int i = 0;
    for (int t = 0; t < targetCount; t++) {
        int idx = (int)(t * step);
        if (idx >= total) idx = total - 1;
        JsonArray kline = arr[idx];
        const char* closeStr = kline[4].as<const char*>();
        float val = closeStr ? atof(closeStr) : 0.0f;
        if (val <= 0) continue;
        if (invert) val = 1.0f / val;
        out.points[i] = val;
        if (val < out.minVal) out.minVal = val;
        if (val > out.maxVal) out.maxVal = val;
        i++;
    }
    out.count = i;

    out.valid = (i >= 2);
    out.lastUpdate = millis();

    Serial.printf("[API] Binance klines %s (%s, %d): %d pts, %.4f-%.4f\n",
                  symbol, interval, limit, out.count, out.minVal, out.maxVal);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── Binance: OHLC with parameterized symbol ──
ApiResult fetchBinanceOhlcSymbol(OhlcData& out, const char* symbol,
                                  const char* interval, int limit, bool invert) {
    char urlBuf[128];
    snprintf(urlBuf, sizeof(urlBuf),
             "%s?symbol=%s&interval=%s&limit=%d",
             BINANCE_KLINES_EP, symbol, interval, limit);

    ApiResult result;
    String json = httpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[API] Binance OHLC JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    int total = arr.size();
    int count = (total <= OHLC_MAX_BARS) ? total : OHLC_MAX_BARS;

    out.count = 0;
    out.minVal = 1e12f;
    out.maxVal = -1e12f;

    for (int i = 0; i < count; i++) {
        JsonArray kline = arr[i];
        const char* openStr  = kline[1].as<const char*>();
        const char* highStr  = kline[2].as<const char*>();
        const char* lowStr   = kline[3].as<const char*>();
        const char* closeStr = kline[4].as<const char*>();

        float o = openStr  ? atof(openStr)  : 0.0f;
        float h = highStr  ? atof(highStr)  : 0.0f;
        float l = lowStr   ? atof(lowStr)   : 0.0f;
        float c = closeStr ? atof(closeStr) : 0.0f;

        if (o <= 0 || h <= 0 || l <= 0 || c <= 0) continue;

        if (invert) {
            // Invert all OHLC values; note high/low swap when inverting
            float io = 1.0f / o;
            float ih = 1.0f / l;   // 1/low becomes high
            float il = 1.0f / h;   // 1/high becomes low
            float ic = 1.0f / c;
            o = io; h = ih; l = il; c = ic;
        }

        out.bars[out.count] = { o, h, l, c };
        if (l < out.minVal) out.minVal = l;
        if (h > out.maxVal) out.maxVal = h;
        out.count++;
    }

    out.valid = (out.count >= 2);
    out.lastUpdate = millis();

    Serial.printf("[API] Binance OHLC %s (%s, %d): %d bars\n",
                  symbol, interval, limit, out.count);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── CoinGecko: Sparkline with configurable vs_currency ──
ApiResult fetchSparklineVsCurrency(SparklineData& out, int days,
                                    const char* vsCurrency, CoinId coin) {
    const char* geckoId = "bitcoin";
    switch (coin) {
        case COIN_ETH: geckoId = "ethereum"; break;
        case COIN_SOL: geckoId = "solana";   break;
        default:       geckoId = "bitcoin";  break;
    }

    char urlBuf[256];
    snprintf(urlBuf, sizeof(urlBuf),
             "https://api.coingecko.com/api/v3/coins/%s/market_chart?vs_currency=%s&days=%d",
             geckoId, vsCurrency, days);

    ApiResult result;
    String json = httpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument filter;
    filter["prices"][0][0] = true;
    filter["prices"][0][1] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(15));

    if (err) {
        Serial.printf("[API] Sparkline JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray prices = doc["prices"];
    int total = prices.size();
    if (total == 0) return API_PARSE_ERROR;

    int targetCount = min((int)SPARKLINE_POINTS, total);
    out.count = targetCount;
    out.minVal = 1e12;
    out.maxVal = -1e12;

    float step = (float)total / targetCount;
    for (int i = 0; i < targetCount; i++) {
        int idx = (int)(i * step);
        if (idx >= total) idx = total - 1;
        float val = prices[idx][1].as<float>();
        out.points[i] = val;
        if (val < out.minVal) out.minVal = val;
        if (val > out.maxVal) out.maxVal = val;
    }

    out.valid = true;
    out.lastUpdate = millis();
    Serial.printf("[API] Sparkline %s vs %s (%dd): %d pts, %.4f-%.4f\n",
                  geckoId, vsCurrency, days, out.count, out.minVal, out.maxVal);
    return API_OK;
}

// ── CoinGecko: Fetch BTC price in arbitrary currency (for XAU, etc.) ──
ApiResult fetchGeckoBtcPrice(const char* vsCurrency, float& outPrice) {
    char urlBuf[128];
    snprintf(urlBuf, sizeof(urlBuf),
             "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=%s",
             vsCurrency);

    ApiResult result;
    String json = httpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        return API_PARSE_ERROR;
    }

    float price = doc["bitcoin"][vsCurrency] | 0.0f;
    if (price <= 0) return API_PARSE_ERROR;

    outPrice = price;
    Serial.printf("[API] BTC/%s: %.4f\n", vsCurrency, outPrice);
    return API_OK;
}

// ── CriptoYa: Lemon USDC/ARS price ──
ApiResult fetchLemonPrice(LemonPrice& out) {
    ApiResult result;
    String json = httpGet(CRIPTOYA_LEMON_EP, false, result);
    if (json.isEmpty()) return result;

    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        Serial.println("[API] Lemon JSON parse error");
        return API_PARSE_ERROR;
    }

    out.ask = doc["totalAsk"] | 0.0f;
    out.bid = doc["totalBid"] | 0.0f;
    out.valid = (out.ask > 0 && out.bid > 0);
    out.lastUpdate = millis();

    Serial.printf("[API] Lemon USDC/ARS: bid=%.2f ask=%.2f\n", out.bid, out.ask);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── Polymarket: Fetch BTC prediction markets mapped to chart timeframe ──
static bool containsCI(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    size_t hLen = strlen(haystack), nLen = strlen(needle);
    if (nLen > hLen) return false;
    for (size_t i = 0; i <= hLen - nLen; i++) {
        bool match = true;
        for (size_t j = 0; j < nLen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static bool isBtcMarket(const char* question, const char* slug) {
    return containsCI(question, "bitcoin") || containsCI(question, "btc") ||
           containsCI(slug, "bitcoin") || containsCI(slug, "btc");
}

static bool isPricePredictionQuestion(const char* question, const char* slug) {
    return containsCI(slug, "btc-updown") ||
           containsCI(question, "up or down") ||
           containsCI(question, "reach") ||
           containsCI(question, "above") ||
           containsCI(question, "below") ||
           containsCI(question, "price");
}

static float parseFloatVar(JsonVariantConst v) {
    if (v.is<float>() || v.is<double>() || v.is<int>() || v.is<long>() || v.is<unsigned long>()) {
        return v.as<float>();
    }
    const char* s = v.as<const char*>();
    return s ? atof(s) : 0.0f;
}

static bool parseOutcomePrices(JsonVariantConst pricesVar, float& yes, float& no) {
    yes = 0.0f;
    no = 0.0f;

    JsonArrayConst pa = pricesVar.as<JsonArrayConst>();
    if (!pa.isNull() && pa.size() >= 2) {
        yes = parseFloatVar(pa[0]);
        no  = parseFloatVar(pa[1]);
        return (yes > 0.0f || no > 0.0f);
    }

    const char* pricesStr = pricesVar.as<const char*>();
    if (pricesStr && pricesStr[0] == '[') {
        JsonDocument pricesDoc;
        if (!deserializeJson(pricesDoc, pricesStr)) {
            JsonArrayConst pa2 = pricesDoc.as<JsonArrayConst>();
            if (pa2.size() >= 2) {
                yes = parseFloatVar(pa2[0]);
                no  = parseFloatVar(pa2[1]);
                return (yes > 0.0f || no > 0.0f);
            }
        }
    }
    return false;
}

static bool parsePolyMarket(JsonObjectConst m, PolyMarket& pm) {
    memset(&pm, 0, sizeof(PolyMarket));

    const char* question = m["question"] | "";
    strncpy(pm.question, question, PM_QUESTION_LEN - 1);
    pm.question[PM_QUESTION_LEN - 1] = '\0';

    const char* condId = m["conditionId"] | "";
    strncpy(pm.conditionId, condId, PM_COND_ID_LEN - 1);
    pm.conditionId[PM_COND_ID_LEN - 1] = '\0';

    bool hasOutcomePrices = parseOutcomePrices(m["outcomePrices"], pm.yesPrice, pm.noPrice);

    if (!hasOutcomePrices) {
        float lastTrade = parseFloatVar(m["lastTradePrice"]);
        float bestBid   = parseFloatVar(m["bestBid"]);
        float bestAsk   = parseFloatVar(m["bestAsk"]);

        float yes = 0.0f;
        if (lastTrade > 0.0f && lastTrade < 1.0f) {
            yes = lastTrade;
        } else if (bestBid > 0.0f && bestBid < 1.0f && bestAsk > 0.0f && bestAsk < 1.0f) {
            yes = (bestBid + bestAsk) * 0.5f;
        } else if (bestBid > 0.0f && bestBid < 1.0f) {
            yes = bestBid;
        } else if (bestAsk > 0.0f && bestAsk < 1.0f) {
            yes = bestAsk;
        }

        if (yes > 0.0f && yes < 1.0f) {
            pm.yesPrice = yes;
            pm.noPrice = 1.0f - yes;
        }
    }

    pm.volume24hr = parseFloatVar(m["volume24hr"]);

    const char* startTimeStr = m["eventStartTime"] | "";
    if (!startTimeStr || startTimeStr[0] == '\0') {
        startTimeStr = m["startDate"] | "";
    }
    strncpy(pm.startTime, startTimeStr, sizeof(pm.startTime) - 1);
    pm.startTime[sizeof(pm.startTime) - 1] = '\0';

    const char* endDateStr = m["endDate"] | "";
    strncpy(pm.endDate, endDateStr, sizeof(pm.endDate) - 1);
    pm.endDate[sizeof(pm.endDate) - 1] = '\0';
    pm.refPrice = 0.0f;
    pm.refPriceValid = false;

    pm.closed = m["closed"] | false;
    pm.valid = (pm.yesPrice >= 0.0f && pm.noPrice >= 0.0f &&
               (pm.yesPrice > 0.0f || pm.noPrice > 0.0f)) &&
               pm.conditionId[0] != '\0';
    return pm.valid;
}

static const char* CHAINLINK_BTC_FEED_ID =
    "0x00039d9e45394f473ab1f050a1b963e6b05351e52d71e507509ada0c95ed75b8";
static const uint8_t CHAINLINK_BTC_ABI_INDEX = 0;
static const uint8_t CHAINLINK_REF_CACHE_SIZE = 8;

struct ChainlinkRefCacheEntry {
    char startTime[32];
    float refPrice;
    bool valid;
};

static ChainlinkRefCacheEntry chainlinkRefCache[CHAINLINK_REF_CACHE_SIZE] = {};
static uint8_t chainlinkRefCacheCount = 0;
static uint8_t chainlinkRefCacheHead = 0;

static bool extractMinuteKey(const char* iso, char* out, size_t outSize) {
    if (!iso || !out || outSize < 17) return false;
    size_t len = strlen(iso);
    if (len < 16) return false;
    memcpy(out, iso, 16);
    out[16] = '\0';
    return true;
}

static bool parseCandlestickOpen(const char* candlestick, float& outOpen) {
    if (!candlestick || !candlestick[0]) return false;
    const char* open = strstr(candlestick, "open:(");
    if (!open) return false;
    const char* val = strstr(open, "val:");
    if (!val) return false;
    val += 4;
    outOpen = atof(val);
    return outOpen > 0.0f;
}

static bool findOpenForMinute(JsonArrayConst nodes, const char* targetMinute, float& outPrice) {
    if (nodes.isNull() || !targetMinute || !targetMinute[0]) return false;

    bool foundPrevious = false;
    float previousOpen = 0.0f;
    char previousMinute[17] = "";

    for (JsonObjectConst node : nodes) {
        const char* bucket = node["bucket"] | "";
        const char* candlestick = node["candlestick"] | "";
        char bucketMinute[17];
        if (!extractMinuteKey(bucket, bucketMinute, sizeof(bucketMinute))) continue;

        float openVal = 0.0f;
        if (!parseCandlestickOpen(candlestick, openVal)) continue;

        int cmp = strcmp(bucketMinute, targetMinute);
        if (cmp == 0) {
            outPrice = openVal;
            return true;
        }

        if (cmp < 0) {
            if (!foundPrevious || strcmp(bucketMinute, previousMinute) > 0) {
                strncpy(previousMinute, bucketMinute, sizeof(previousMinute) - 1);
                previousMinute[sizeof(previousMinute) - 1] = '\0';
                previousOpen = openVal;
                foundPrevious = true;
            }
        }
    }

    if (foundPrevious) {
        outPrice = previousOpen;
        return true;
    }

    return false;
}

static bool chainlinkRefCacheLookup(const char* startTime, float& outPrice) {
    if (!startTime || !startTime[0]) return false;
    for (uint8_t i = 0; i < chainlinkRefCacheCount; i++) {
        if (strcmp(chainlinkRefCache[i].startTime, startTime) == 0) {
            if (chainlinkRefCache[i].valid) {
                outPrice = chainlinkRefCache[i].refPrice;
                return true;
            }
            return false;
        }
    }
    return false;
}

static bool chainlinkRefCacheHasInvalid(const char* startTime) {
    if (!startTime || !startTime[0]) return false;
    for (uint8_t i = 0; i < chainlinkRefCacheCount; i++) {
        if (strcmp(chainlinkRefCache[i].startTime, startTime) == 0 && !chainlinkRefCache[i].valid) {
            return true;
        }
    }
    return false;
}

static void chainlinkRefCacheStore(const char* startTime, float refPrice, bool valid) {
    if (!startTime || !startTime[0]) return;

    for (uint8_t i = 0; i < chainlinkRefCacheCount; i++) {
        if (strcmp(chainlinkRefCache[i].startTime, startTime) == 0) {
            chainlinkRefCache[i].refPrice = refPrice;
            chainlinkRefCache[i].valid = valid;
            return;
        }
    }

    uint8_t idx;
    if (chainlinkRefCacheCount < CHAINLINK_REF_CACHE_SIZE) {
        idx = chainlinkRefCacheCount++;
    } else {
        idx = chainlinkRefCacheHead;
        chainlinkRefCacheHead = (uint8_t)((chainlinkRefCacheHead + 1) % CHAINLINK_REF_CACHE_SIZE);
    }

    strncpy(chainlinkRefCache[idx].startTime, startTime, sizeof(chainlinkRefCache[idx].startTime) - 1);
    chainlinkRefCache[idx].startTime[sizeof(chainlinkRefCache[idx].startTime) - 1] = '\0';
    chainlinkRefCache[idx].refPrice = refPrice;
    chainlinkRefCache[idx].valid = valid;
}

static bool fetchChainlinkReferencePrice(const char* startTime, float& outPrice) {
    if (!startTime || !startTime[0]) return false;
    if (chainlinkRefCacheLookup(startTime, outPrice)) return true;
    if (chainlinkRefCacheHasInvalid(startTime)) return false;

    char targetMinute[17];
    if (!extractMinuteKey(startTime, targetMinute, sizeof(targetMinute))) {
        chainlinkRefCacheStore(startTime, 0.0f, false);
        return false;
    }

    char urlBuf[320];
    snprintf(urlBuf, sizeof(urlBuf),
             "https://data.chain.link/api/historical-data-engine-stream-data?feedId=%s&abiIndex=%u&timeRange=1D",
             CHAINLINK_BTC_FEED_ID, (unsigned)CHAINLINK_BTC_ABI_INDEX);

    ApiResult result;
    String json = httpGet(urlBuf, false, result);
    if (result != API_OK || json.isEmpty()) {
        chainlinkRefCacheStore(startTime, 0.0f, false);
        return false;
    }

    JsonDocument filter;
    filter["data"]["allStreamValuesGeneric1Minutes"]["nodes"][0]["bucket"] = true;
    filter["data"]["allStreamValuesGeneric1Minutes"]["nodes"][0]["candlestick"] = true;
    filter["data"]["allStreamValuesGeneric1Hours"]["nodes"][0]["bucket"] = true;
    filter["data"]["allStreamValuesGeneric1Hours"]["nodes"][0]["candlestick"] = true;
    filter["data"]["allStreamValuesGeneric1Days"]["nodes"][0]["bucket"] = true;
    filter["data"]["allStreamValuesGeneric1Days"]["nodes"][0]["candlestick"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(12));
    json = String();
    if (err) {
        chainlinkRefCacheStore(startTime, 0.0f, false);
        return false;
    }

    float ref = 0.0f;
    JsonArrayConst minuteNodes = doc["data"]["allStreamValuesGeneric1Minutes"]["nodes"].as<JsonArrayConst>();
    if (!findOpenForMinute(minuteNodes, targetMinute, ref)) {
        JsonArrayConst hourNodes = doc["data"]["allStreamValuesGeneric1Hours"]["nodes"].as<JsonArrayConst>();
        if (!findOpenForMinute(hourNodes, targetMinute, ref)) {
            JsonArrayConst dayNodes = doc["data"]["allStreamValuesGeneric1Days"]["nodes"].as<JsonArrayConst>();
            if (!findOpenForMinute(dayNodes, targetMinute, ref)) {
                chainlinkRefCacheStore(startTime, 0.0f, false);
                return false;
            }
        }
    }

    outPrice = ref;
    chainlinkRefCacheStore(startTime, ref, true);
    Serial.printf("[API] Chainlink ref: start=%s target=%s price=%.2f\n",
                  startTime, targetMinute, ref);
    return true;
}

void enrichPolyReference(PolyMarket& pm) {
    pm.refPrice = 0.0f;
    pm.refPriceValid = false;
    if (pm.startTime[0] == '\0') return;

    float ref = 0.0f;
    if (fetchChainlinkReferencePrice(pm.startTime, ref) && ref > 0.0f) {
        pm.refPrice = ref;
        pm.refPriceValid = true;
    }
}

struct PolyUpDownSpec {
    const char* tf;
    uint32_t stepSec;
    uint32_t offsetSec;
};

static bool getUpDownSpecForPeriod(uint8_t btcPeriod, PolyUpDownSpec& spec) {
    switch (btcPeriod) {
        case 0: spec = { "5m",   300,   0 }; break;
        case 1: spec = { "15m",  900,   0 }; break;
        case 2: return false;                             // 1h: no short market yet
        case 3: spec = { "4h", 14400, 3600 }; break;     // 4h markets use +1h offset
        case 4: return false;                             // 24h: no short market yet
        case 5: return false;
        case 6: return false;
        case 7: return false;
        default: return false;
    }
    return true;
}

// Extract Unix timestamp from btc-updown slug and write ISO 8601 into startTime.
// Slug format: "btc-updown-{tf}-{unix_ts}"  e.g. "btc-updown-5m-1739984400"
// The eventStartTime field from the API is the event CREATION date — NOT the
// interval start — which can be hours/days earlier.  The slug timestamp IS the
// correct interval start and must be used for the Chainlink reference lookup.
static void overrideStartTimeFromSlug(const char* slug, PolyMarket& pm) {
    if (!slug) return;
    // Find the last '-' to locate the timestamp portion
    const char* last = strrchr(slug, '-');
    if (!last || *(last + 1) == '\0') return;
    int64_t ts = strtoll(last + 1, nullptr, 10);
    if (ts < 1700000000) return;  // sanity: must be after 2023
    time_t t = (time_t)ts;
    struct tm utc;
    gmtime_r(&t, &utc);
    snprintf(pm.startTime, sizeof(pm.startTime),
             "%04d-%02d-%02dT%02d:%02d:%02dZ",
             utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
             utc.tm_hour, utc.tm_min, utc.tm_sec);
}

static ApiResult fetchPolyFromEventSlug(const char* slug, PolyMarket* out, uint8_t& count) {
    count = 0;

    char urlBuf[220];
    snprintf(urlBuf, sizeof(urlBuf),
             "https://gamma-api.polymarket.com/events?slug=%s",
             slug);

    ApiResult result;
    String json = httpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument filter;
    filter[0]["markets"][0]["question"] = true;
    filter[0]["markets"][0]["slug"] = true;
    filter[0]["markets"][0]["conditionId"] = true;
    filter[0]["markets"][0]["outcomePrices"] = true;
    filter[0]["markets"][0]["lastTradePrice"] = true;
    filter[0]["markets"][0]["bestBid"] = true;
    filter[0]["markets"][0]["bestAsk"] = true;
    filter[0]["markets"][0]["volume24hr"] = true;
    filter[0]["markets"][0]["endDate"] = true;
    filter[0]["markets"][0]["eventStartTime"] = true;
    filter[0]["markets"][0]["startDate"] = true;
    filter[0]["markets"][0]["closed"] = true;
    filter[0]["startTime"] = true;
    filter[0]["startDate"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(12));
    json = String();
    if (err) {
        Serial.printf("[API] Polymarket event parse error (%s): %s\n", slug, err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArrayConst events = doc.as<JsonArrayConst>();
    if (events.isNull() || events.size() == 0) return API_PARSE_ERROR;

    JsonArrayConst markets = events[0]["markets"].as<JsonArrayConst>();
    if (markets.isNull() || markets.size() == 0) return API_PARSE_ERROR;

    JsonObjectConst m = markets[0].as<JsonObjectConst>();
    if (!parsePolyMarket(m, out[0])) return API_PARSE_ERROR;

    if (out[0].startTime[0] == '\0') {
        const char* evtStart = events[0]["startTime"] | "";
        if (!evtStart || evtStart[0] == '\0') {
            evtStart = events[0]["startDate"] | "";
        }
        strncpy(out[0].startTime, evtStart, sizeof(out[0].startTime) - 1);
        out[0].startTime[sizeof(out[0].startTime) - 1] = '\0';
    }

    count = 1;
    return API_OK;
}

static ApiResult fetchPolyUpDownForPeriod(PolyMarket* out, uint8_t& count, uint8_t btcPeriod) {
    count = 0;

    PolyUpDownSpec spec = {};
    if (!getUpDownSpecForPeriod(btcPeriod, spec)) return API_PARSE_ERROR;

    time_t now = time(nullptr);
    if (now < 1700000000) {
        Serial.println("[API] Polymarket up/down: invalid epoch (NTP not ready?)");
        return API_PARSE_ERROR;
    }

    int64_t baseTs = ((int64_t)now - (int64_t)spec.offsetSec) / (int64_t)spec.stepSec;
    baseTs = baseTs * (int64_t)spec.stepSec + (int64_t)spec.offsetSec;

    Serial.printf("[API] Polymarket up/down: now=%lld base=%lld step=%lu tf=%s\n",
                  (long long)now, (long long)baseTs, (unsigned long)spec.stepSec, spec.tf);

    static const int8_t CANDIDATE_BUCKETS[] = { 0, -1, 1, -2 };
    for (int8_t delta : CANDIDATE_BUCKETS) {
        int64_t ts = baseTs + (int64_t)delta * (int64_t)spec.stepSec;
        char slug[64];
        snprintf(slug, sizeof(slug), "btc-updown-%s-%lld", spec.tf, (long long)ts);

        ApiResult res = fetchPolyFromEventSlug(slug, out, count);
        if (res == API_OK && count > 0) {
            overrideStartTimeFromSlug(slug, out[0]);
            Serial.printf("[API] Polymarket up/down match: %s delta=%d start=%s\n",
                          slug, (int)delta, out[0].startTime);
            return API_OK;
        }
        Serial.printf("[API] Polymarket slug miss: %s (delta=%d)\n", slug, (int)delta);
    }

    Serial.printf("[API] Polymarket up/down miss for period idx %d (%s)\n", btcPeriod, spec.tf);
    return API_PARSE_ERROR;
}

static ApiResult fetchPolyUpDownRecent(PolyMarket* out, uint8_t& count, uint8_t btcPeriod) {
    count = 0;

    PolyUpDownSpec spec = {};
    if (!getUpDownSpecForPeriod(btcPeriod, spec)) return API_PARSE_ERROR;

    char prefix[24];
    snprintf(prefix, sizeof(prefix), "btc-updown-%s-", spec.tf);

    static const uint16_t PAGE_LIMIT = 20;
    static const uint16_t MAX_OFFSET = 600;
    for (uint16_t offset = 0; offset <= MAX_OFFSET; offset += PAGE_LIMIT) {
        char urlBuf[260];
        snprintf(urlBuf, sizeof(urlBuf),
                 "%s?active=true&closed=false&order=createdAt&ascending=false&limit=%u&offset=%u",
                 POLYMARKET_GAMMA_URL, (unsigned)PAGE_LIMIT, (unsigned)offset);

        ApiResult result;
        String json = httpGet(urlBuf, false, result);
        if (result != API_OK) return result;

        JsonDocument filter;
        filter[0]["question"] = true;
        filter[0]["slug"] = true;
        filter[0]["conditionId"] = true;
        filter[0]["outcomePrices"] = true;
        filter[0]["lastTradePrice"] = true;
        filter[0]["bestBid"] = true;
        filter[0]["bestAsk"] = true;
        filter[0]["volume24hr"] = true;
        filter[0]["endDate"] = true;
        filter[0]["eventStartTime"] = true;
        filter[0]["startDate"] = true;
        filter[0]["closed"] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, json,
            DeserializationOption::Filter(filter),
            DeserializationOption::NestingLimit(10));
        json = String();
        if (err) {
            Serial.printf("[API] Polymarket recent parse error (off=%u): %s\n",
                          (unsigned)offset, err.c_str());
            return API_PARSE_ERROR;
        }

        JsonArrayConst arr = doc.as<JsonArrayConst>();
        if (arr.isNull() || arr.size() == 0) break;

        for (JsonObjectConst m : arr) {
            const char* slug = m["slug"] | "";
            if (strncmp(slug, prefix, strlen(prefix)) != 0) continue;

            PolyMarket pm = {};
            if (parsePolyMarket(m, pm)) {
                out[0] = pm;
                overrideStartTimeFromSlug(slug, out[0]);
                count = 1;
                Serial.printf("[API] Polymarket up/down recent match: %s (start=%s)\n", slug, out[0].startTime);
                return API_OK;
            }

            // Fallback: pull event detail for this slug if compact market payload is incomplete.
            ApiResult bySlug = fetchPolyFromEventSlug(slug, out, count);
            if (bySlug == API_OK && count > 0) {
                overrideStartTimeFromSlug(slug, out[0]);
                Serial.printf("[API] Polymarket up/down recent match (event): %s (start=%s)\n", slug, out[0].startTime);
                return API_OK;
            }
        }
    }

    Serial.printf("[API] Polymarket up/down recent miss for period idx %d (%s)\n", btcPeriod, spec.tf);
    return API_PARSE_ERROR;
}

static ApiResult fetchPolyBtcFallback(PolyMarket* out, uint8_t& count, uint8_t limit) {
    char urlBuf[220];
    snprintf(urlBuf, sizeof(urlBuf),
             "%s?active=true&closed=false&order=volume24hr&ascending=false&limit=120",
             POLYMARKET_GAMMA_URL);

    ApiResult result;
    String json = httpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument filter;
    filter[0]["question"] = true;
    filter[0]["slug"] = true;
    filter[0]["conditionId"] = true;
    filter[0]["outcomePrices"] = true;
    filter[0]["lastTradePrice"] = true;
    filter[0]["bestBid"] = true;
    filter[0]["bestAsk"] = true;
    filter[0]["volume24hr"] = true;
    filter[0]["endDate"] = true;
    filter[0]["eventStartTime"] = true;
    filter[0]["startDate"] = true;
    filter[0]["closed"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(10));
    json = String();
    if (err) return API_PARSE_ERROR;

    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    count = 0;

    for (JsonObjectConst m : arr) {
        if (count >= limit) break;
        const char* q = m["question"] | "";
        const char* slug = m["slug"] | "";
        if (!isBtcMarket(q, slug) || !isPricePredictionQuestion(q, slug)) continue;

        PolyMarket pm = {};
        if (!parsePolyMarket(m, pm)) continue;
        out[count++] = pm;
    }

    if (count < limit) {
        for (JsonObjectConst m : arr) {
            if (count >= limit) break;
            const char* q = m["question"] | "";
            const char* slug = m["slug"] | "";
            if (!isBtcMarket(q, slug) || isPricePredictionQuestion(q, slug)) continue;

            PolyMarket pm = {};
            if (!parsePolyMarket(m, pm)) continue;

            bool dup = false;
            for (uint8_t i = 0; i < count; i++) {
                if (strcmp(out[i].conditionId, pm.conditionId) == 0) {
                    dup = true;
                    break;
                }
            }
            if (!dup) out[count++] = pm;
        }
    }

    Serial.printf("[API] Polymarket fallback BTC markets: %d\n", count);
    return (count > 0) ? API_OK : API_PARSE_ERROR;
}

ApiResult fetchPolyMarkets(PolyMarket* out, uint8_t& count, uint8_t limit, uint8_t btcPeriod) {
    count = 0;
    if (!out || limit == 0) return API_PARSE_ERROR;
    if (limit > PM_MAX_MARKETS) limit = PM_MAX_MARKETS;

    ApiResult tsRes = fetchPolyUpDownForPeriod(out, count, btcPeriod);
    if (tsRes == API_OK && count > 0) {
        // Markets returned without Chainlink enrichment — caller handles that
        return API_OK;
    }

    Serial.printf("[API] Polymarket miss for period idx=%d (tsRes=%d)\n",
                  btcPeriod, (int)tsRes);
    return tsRes;
}

ApiResult fetchPolyMarketByConditionId(const char* conditionId, PolyMarket& out) {
    memset(&out, 0, sizeof(out));
    if (!conditionId || conditionId[0] == '\0') return API_PARSE_ERROR;

    char urlBuf[280];
    snprintf(urlBuf, sizeof(urlBuf), "%s?condition_ids=%s&limit=1",
             POLYMARKET_GAMMA_URL, conditionId);

    ApiResult result;
    String json = httpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument filter;
    filter[0]["question"] = true;
    filter[0]["slug"] = true;
    filter[0]["conditionId"] = true;
    filter[0]["outcomePrices"] = true;
    filter[0]["lastTradePrice"] = true;
    filter[0]["bestBid"] = true;
    filter[0]["bestAsk"] = true;
    filter[0]["volume24hr"] = true;
    filter[0]["endDate"] = true;
    filter[0]["eventStartTime"] = true;
    filter[0]["startDate"] = true;
    filter[0]["closed"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(10));
    json = String();
    if (err) return API_PARSE_ERROR;

    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    for (JsonObjectConst m : arr) {
        const char* cond = m["conditionId"] | "";
        if (strcmp(cond, conditionId) != 0) continue;
        if (!parsePolyMarket(m, out)) return API_PARSE_ERROR;
        return API_OK;
    }

    return API_PARSE_ERROR;
}

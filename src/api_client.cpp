#include "api_client.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

static WiFiClientSecure secureClient;

void apiSetup() {
    secureClient.setInsecure(); // Skip cert validation (ESP32 has limited CA store)
}

// ── Helper: perform HTTPS GET with 1 retry ──
static String httpGet(const char* url, bool addCoinGeckoKey, ApiResult& result) {
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) {
            Serial.printf("[API] Retry %d for %s\n", attempt, url);
            delay(2000);
        }

        HTTPClient http;
        http.setConnectTimeout(5000);
        http.setTimeout(10000);

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

// ── CoinGecko: Tether/ARS sparkline (for dollar chart) ──
ApiResult fetchLemonSparkline(SparklineData& out, int days) {
    char urlBuf[128];
    snprintf(urlBuf, sizeof(urlBuf), "%s%d", COINGECKO_TETHER_CHART_EP, days);

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
        Serial.printf("[API] Lemon sparkline JSON error: %s\n", err.c_str());
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
    Serial.printf("[API] Lemon sparkline (%dd): %d pts, $%.0f-$%.0f\n",
                  days, out.count, out.minVal, out.maxVal);
    return API_OK;
}

// ── CriptoYa: Lemon USDT/ARS price ──
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

    Serial.printf("[API] Lemon USDT/ARS: bid=%.2f ask=%.2f\n", out.bid, out.ask);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

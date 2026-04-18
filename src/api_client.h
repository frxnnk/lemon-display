#pragma once

#include "data_models.h"

enum ApiResult : uint8_t {
    API_OK,
    API_NETWORK_ERROR,
    API_PARSE_ERROR,
    API_TIMEOUT,
    API_RATE_LIMITED
};

void apiSetup();
void apiStop();  // Release TLS session to free heap before OTA

// Shared HTTPS GET helper. Response is written into a reusable PSRAM buffer
// owned by api_client; returned pointer is valid until the next call.
// Handles chunked Transfer-Encoding, TLS timeouts, WDT resets, and a single
// retry on empty body. Intended for use by sibling client modules
// (stocks_client, poly_client) that want the same hardened code path.
const char* apiHttpGet(const char* url, bool addCoinGeckoKey, ApiResult& result,
                       int timeoutMs = 7000);

// Each returns ApiResult. On failure, struct is left unchanged.
ApiResult fetchBtcPrice(BtcPrice& out);
ApiResult fetchBtcPriceSimple(BtcPrice& out);
ApiResult fetchGlobalData(CryptoGlobal& out);
ApiResult fetchSparkline(SparklineData& out, int days = 7, CoinId coin = COIN_BTC);
ApiResult fetchMarketData(MarketData& out);
ApiResult fetchLemonPrice(LemonPrice& out);
ApiResult fetchBinanceKlines(SparklineData& out, const char* interval, int limit);
ApiResult fetchBinanceOhlc(OhlcData& out, const char* interval, int limit);
ApiResult fetchLemonSparkline(SparklineData& out, int days);

// Parameterized variants for pair switching
ApiResult fetchBinanceKlinesSymbol(SparklineData& out, const char* symbol,
                                    const char* interval, int limit, bool invert = false);
ApiResult fetchBinanceOhlcSymbol(OhlcData& out, const char* symbol,
                                  const char* interval, int limit, bool invert = false);
ApiResult fetchSparklineVsCurrency(SparklineData& out, int days,
                                    const char* vsCurrency, CoinId coin = COIN_BTC);
ApiResult fetchGeckoBtcPrice(const char* vsCurrency, float& outPrice);

// Polymarket: fetch BTC prediction markets mapped to selected BTC period
// Note: markets are returned WITHOUT Chainlink reference price (refPriceValid=false).
// Call enrichPolyReference() separately to fill in the threshold price.
ApiResult fetchPolyMarkets(PolyMarket* out, uint8_t& count, uint8_t limit = 3,
                           uint8_t btcPeriod = 4);
ApiResult fetchPolyMarketByConditionId(const char* conditionId, PolyMarket& out);

// Parse ISO 8601 "YYYY-MM-DDTHH:MM:SSZ" → UTC epoch seconds
uint32_t isoToEpoch(const char* iso);

// Enrich a market with Chainlink BTC/USD reference price at its start time.
// Separate from fetchPolyMarkets so UI can render immediately while this loads.
void enrichPolyReference(PolyMarket& pm);

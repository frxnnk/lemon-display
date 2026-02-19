#pragma once

#include "data_models.h"

enum ApiResult : uint8_t {
    API_OK,
    API_NETWORK_ERROR,
    API_PARSE_ERROR,
    API_TIMEOUT
};

void apiSetup();

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
ApiResult fetchPolyMarkets(PolyMarket* out, uint8_t& count, uint8_t limit = 3,
                           uint8_t btcPeriod = 4);
ApiResult fetchPolyMarketByConditionId(const char* conditionId, PolyMarket& out);

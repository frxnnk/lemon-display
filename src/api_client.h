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

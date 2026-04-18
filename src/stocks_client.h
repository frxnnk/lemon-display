#pragma once

#include "data_models.h"
#include "api_client.h"   // ApiResult

// Fetch regular-market quotes for up to STOCK_MAX_SYMBOLS tickers. The
// `symbols` argument is a comma-separated list (e.g. "AAPL,TSLA,NVDA").
// Writes `out[i]` for each successfully parsed symbol, in the order they
// appear in the response (which matches request order). `outCount` is set
// to the number of valid entries on success.
ApiResult fetchStockQuotes(const char* csvSymbols, StockQuote* out, int maxOut, int& outCount);

// Fetch a price chart for a single symbol. `range` / `interval` are Yahoo's
// chart parameters — e.g. range="1d" interval="5m", range="1y" interval="1d".
ApiResult fetchStockChart(const char* symbol, const char* range, const char* interval,
                          SparklineData& out);

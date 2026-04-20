#pragma once

#include "data_models.h"
#include "api_client.h"   // ApiResult

// Yahoo capped /v7/finance/quote (401 without crumb/cookie in 2026-Q1), so we
// use /v8/finance/chart/ for both quote and sparkline in a single request.
// Writes `quote` (symbol, name, price, change, change%, day high/low) and
// `spark` (close[] downsampled into SparklineData).
ApiResult fetchStockChart(const char* symbol, const char* range, const char* interval,
                          StockQuote& quote, SparklineData& spark);

// Release the dedicated TLS session and HTTP state so the heap can be
// reclaimed before OTA. Safe to call at any time.
void stocksClientStop();

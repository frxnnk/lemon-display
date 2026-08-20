#pragma once

#include <cstdint>
#include "usdt_lemon_model.h"

struct UsdtPriceData {
    float ars = 0.0f;
    float bid = 0.0f;
    float ask = 0.0f;
    uint32_t lastUpdateMs = 0;
    bool valid = false;
};

constexpr uint8_t USDT_PEG_SAMPLE_COUNT = 24;
constexpr uint8_t USDT_ARS_CHART_POINT_COUNT = 48;

struct UsdtPegData {
    float usd = 0.0f;
    float ars = 0.0f;
    float change1h = 0.0f;
    float change24h = 0.0f;
    float change7d = 0.0f;
    float brl = 0.0f;
    float pen = 0.0f;
    float cop = 0.0f;
    float pegSamples[USDT_PEG_SAMPLE_COUNT] = {};
    uint8_t pegSampleCount = 0;
    float arsChart[USDT_ARS_CHART_POINT_COUNT] = {};
    uint8_t arsChartCount = 0;
    float usdChart[USDT_ARS_CHART_POINT_COUNT] = {};
    uint8_t usdChartCount = 0;
    uint32_t lastUpdateMs = 0;
    uint32_t variationsLastUpdateMs = 0;
    uint32_t variationsLastAttemptMs = 0;
    uint32_t usdChartLastUpdateMs = 0;
    uint32_t usdChartLastAttemptMs = 0;
    uint32_t regionsLastUpdateMs = 0;
    bool valid = false;
    bool variationsValid = false;
    bool usdChartValid = false;
    bool regionsValid = false;
};

struct UsdtYieldData {
    float aprPercent = 0.0f;
    uint32_t lastUpdateMs = 0;
    bool valid = false;
};

enum UsdtNetworkIndex : uint8_t {
    USDT_NETWORK_BNB = 0,
    USDT_NETWORK_POLYGON,
    USDT_NETWORK_TRON,
    USDT_NETWORK_ETHEREUM,
    USDT_NETWORK_COUNT,
};

struct UsdtNetworkMetric {
    float supplyUsd = 0.0f;
    float change24h = 0.0f;
    bool valid = false;
};

struct UsdtNetworkData {
    UsdtNetworkMetric metrics[USDT_NETWORK_COUNT] = {};
    uint32_t lastUpdateMs = 0;
    uint32_t lastAttemptMs = 0;
    bool valid = false;
};

struct UsdtOtaState {
    bool checked = false;
    bool checking = false;
    bool available = false;
    bool failed = false;
    char version[16] = {};
};

struct UsdtDataSnapshot {
    UsdtPriceData lemon = {};
    UsdtPegData peg = {};
    UsdtYieldData yield = {};
    UsdtNetworkData networks = {};
    UsdtFetchStatus lemonStatus = USDT_FETCH_NETWORK_ERROR;
    UsdtFetchStatus pegStatus = USDT_FETCH_NETWORK_ERROR;
    UsdtFetchStatus yieldStatus = USDT_FETCH_NETWORK_ERROR;
    UsdtFetchStatus variationsStatus = USDT_FETCH_NETWORK_ERROR;
    UsdtFetchStatus usdChartStatus = USDT_FETCH_NETWORK_ERROR;
    UsdtFetchStatus networksStatus = USDT_FETCH_NETWORK_ERROR;
    UsdtFreshness lemonFreshness = USDT_LOADING;
    UsdtFreshness pegFreshness = USDT_LOADING;
    UsdtFreshness yieldFreshness = USDT_LOADING;
    UsdtFreshness networksFreshness = USDT_LOADING;
    UsdtOtaState ota = {};
    bool online = false;
    bool fetching = false;
};

void usdtDataSetup();
bool usdtDataFetchPrice(UsdtDataSnapshot& io);
bool usdtDataFetchRates(UsdtDataSnapshot& io);
bool usdtDataFetchYield(UsdtDataSnapshot& io);
bool usdtDataFetchVariations(UsdtDataSnapshot& io);
bool usdtDataFetchUsdChart(UsdtDataSnapshot& io);
bool usdtDataFetchNetworks(UsdtDataSnapshot& io);
void usdtDataUpdateFreshness(UsdtDataSnapshot& io, uint32_t nowMs, bool online);
const char* usdtFreshnessLabel(UsdtFreshness freshness);
const char* usdtFetchStatusLabel(UsdtFetchStatus status);

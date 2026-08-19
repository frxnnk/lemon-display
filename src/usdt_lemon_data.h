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

struct UsdtPegData {
    float usd = 0.0f;
    float ars = 0.0f;
    float change1h = 0.0f;
    float change24h = 0.0f;
    float change7d = 0.0f;
    float brl = 0.0f;
    float mxn = 0.0f;
    uint32_t lastUpdateMs = 0;
    bool valid = false;
};

struct UsdtYieldData {
    float aprPercent = 0.0f;
    uint32_t lastUpdateMs = 0;
    bool valid = false;
};

struct UsdtOtaState {
    bool checked = false;
    bool checking = false;
    bool available = false;
    char version[16] = {};
};

struct UsdtDataSnapshot {
    UsdtPriceData lemon = {};
    UsdtPegData peg = {};
    UsdtYieldData yield = {};
    UsdtFetchStatus lemonStatus = USDT_FETCH_NETWORK_ERROR;
    UsdtFetchStatus pegStatus = USDT_FETCH_NETWORK_ERROR;
    UsdtFetchStatus yieldStatus = USDT_FETCH_NETWORK_ERROR;
    UsdtFreshness lemonFreshness = USDT_LOADING;
    UsdtFreshness pegFreshness = USDT_LOADING;
    UsdtFreshness yieldFreshness = USDT_LOADING;
    UsdtOtaState ota = {};
    bool online = false;
    bool fetching = false;
};

void usdtDataSetup();
bool usdtDataFetch(UsdtDataSnapshot& io);
void usdtDataUpdateFreshness(UsdtDataSnapshot& io, uint32_t nowMs, bool online);
const char* usdtFreshnessLabel(UsdtFreshness freshness);

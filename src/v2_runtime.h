#pragma once

#include "api_client.h"
#include "data_models.h"
#include "ui_stocks.h"
#include "v2_runtime_model.h"

static const uint8_t V2_TAPE_ROWS = 5;

struct V2RuntimeSnapshot {
    BtcPrice btc = {};
    LemonPrice lemon = {};
    SparklineData pairSpark = {};
    SparklineData lemonSpark = {};
    ApiResult btcResult = API_NETWORK_ERROR;
    ApiResult lemonResult = API_NETWORK_ERROR;
    V2Freshness btcFreshness = V2_LOADING;
    V2Freshness pairFreshness = V2_LOADING;
    V2Freshness lemonFreshness = V2_LOADING;
    float pairPrice = 0.0f;
    float lemonChange24h = 0.0f;
    uint32_t pairLastUpdate = 0;
    uint8_t selectedPair = 0;
    bool pairValid = false;
    bool lemonChange24hValid = false;
    bool pairFetching = false;
    char pairSource[24] = {};
    StockFocusedSnapshot stocks[V2_TAPE_ROWS] = {};
    uint8_t stockCount = 0;
    uint8_t focusedStock = 0;
    bool online = false;
    bool btcFetching = false;
    bool lemonFetching = false;
    bool stocksFetching = false;
    bool wifiResetArmed = false;
    bool otaChecked = false;
    bool otaAvailable = false;
    bool otaChecking = false;
    bool otaArmed = false;
    char otaVersion[16] = {};
    char time[12] = "--:--:--";
    char ssid[33] = {};
    char ip[20] = {};
    int32_t rssi = 0;
    uint8_t brightness = 0;
    uint8_t rotationSeconds = 0;
    bool use24h = true;
    bool soundEnabled = true;
    uint32_t uptimeSeconds = 0;
    uint32_t freeHeap = 0;
    StockNews news[NEWS_MAX_ITEMS] = {};
    uint8_t newsCount = 0;
    bool newsFetching = false;
};

void v2RuntimeSetup();
void v2RuntimeLoop();

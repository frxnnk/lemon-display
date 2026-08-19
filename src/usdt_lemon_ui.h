#pragma once

#include "usdt_lemon_data.h"
#include "usdt_lemon_model.h"

struct UsdtDeviceInfo {
    char time[12] = "--:--";
    char ssid[33] = {};
    char ip[20] = {};
    int32_t rssi = 0;
    uint32_t uptimeSeconds = 0;
    uint32_t freeHeap = 0;
    bool provisioning = false;
};

bool usdtUiSetup();
void usdtUiDrawLoading(const char* message, uint8_t progress);
void usdtUiDraw(const UsdtDataSnapshot& data, const UsdtRuntimeModel& model,
                const UsdtDeviceInfo& device);

#pragma once

#include "ota_manager.h"
#include "usdt_lemon_data.h"

enum UsdtWorkerUpdateKind : uint8_t {
    USDT_WORKER_DATA_PARTIAL = 0,
    USDT_WORKER_DATA_COMPLETE,
    USDT_WORKER_PRICE_COMPLETE,
    USDT_WORKER_OTA_CHECK,
    USDT_WORKER_OTA_PROBE,
};

struct UsdtWorkerUpdate {
    UsdtWorkerUpdateKind kind = USDT_WORKER_DATA_PARTIAL;
    UsdtDataSnapshot data = {};
    OtaInfo ota = {};
    bool tagChanged = false;
};

bool usdtWorkerSetup();
bool usdtWorkerBusy();
bool usdtWorkerRequestData(const UsdtDataSnapshot& seed);
bool usdtWorkerRequestPrice(const UsdtDataSnapshot& seed);
bool usdtWorkerRequestOtaCheck();
bool usdtWorkerRequestOtaProbe();
bool usdtWorkerPoll(UsdtWorkerUpdate& update);

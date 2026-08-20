#include "usdt_lemon_worker.h"

#include "config.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {
enum UsdtWorkerJob : uint8_t {
    USDT_JOB_DATA = 0,
    USDT_JOB_PRICE,
    USDT_JOB_OTA_CHECK,
    USDT_JOB_OTA_PROBE,
};

struct UsdtWorkerCommand {
    UsdtWorkerJob job = USDT_JOB_DATA;
    UsdtDataSnapshot data = {};
};

QueueHandle_t s_commands = nullptr;
QueueHandle_t s_updates = nullptr;
TaskHandle_t s_task = nullptr;
volatile bool s_busy = false;

void publishData(const UsdtDataSnapshot& data, bool complete) {
    UsdtWorkerUpdate update = {};
    update.kind = complete ? USDT_WORKER_DATA_COMPLETE
                           : USDT_WORKER_DATA_PARTIAL;
    update.data = data;
    xQueueSend(s_updates, &update, portMAX_DELAY);
}

void fetchData(UsdtDataSnapshot data) {
    data.fetching = true;
    usdtDataFetchPrice(data);
    publishData(data, false);
    usdtDataFetchRates(data);
    publishData(data, false);
    usdtDataFetchYield(data);
    publishData(data, false);
    usdtDataFetchVariations(data);
    publishData(data, false);
    usdtDataFetchNetworks(data);
    data.fetching = false;
    publishData(data, true);
}

void fetchPrice(UsdtDataSnapshot data) {
    usdtDataFetchPrice(data);
    UsdtWorkerUpdate update = {};
    update.kind = USDT_WORKER_PRICE_COMPLETE;
    update.data = data;
    xQueueSend(s_updates, &update, portMAX_DELAY);
}

void workerTask(void*) {
    UsdtWorkerCommand command = {};
    while (true) {
        if (xQueueReceive(s_commands, &command, portMAX_DELAY) != pdTRUE) continue;
        if (command.job == USDT_JOB_DATA) {
            fetchData(command.data);
        } else if (command.job == USDT_JOB_PRICE) {
            fetchPrice(command.data);
        } else if (command.job == USDT_JOB_OTA_CHECK) {
            UsdtWorkerUpdate update = {};
            update.kind = USDT_WORKER_OTA_CHECK;
            update.ota = otaCheckAsset(
                OTA_GITHUB_REPO, OTA_USDT_ASSET, APP_VERSION);
            xQueueSend(s_updates, &update, portMAX_DELAY);
        } else {
            UsdtWorkerUpdate update = {};
            update.kind = USDT_WORKER_OTA_PROBE;
            update.tagChanged = otaLatestTagChanged(
                OTA_GITHUB_REPO, APP_VERSION);
            xQueueSend(s_updates, &update, portMAX_DELAY);
        }
        s_busy = false;
    }
}

bool request(const UsdtWorkerCommand& command) {
    if (!s_commands || s_busy) return false;
    s_busy = true;
    if (xQueueSend(s_commands, &command, 0) == pdTRUE) return true;
    s_busy = false;
    return false;
}
}  // namespace

bool usdtWorkerSetup() {
    if (s_task) return true;
    s_commands = xQueueCreate(1, sizeof(UsdtWorkerCommand));
    s_updates = xQueueCreate(6, sizeof(UsdtWorkerUpdate));
    if (!s_commands || !s_updates) return false;
    return xTaskCreatePinnedToCore(
        workerTask, "usdt-net", 16384, nullptr, 1, &s_task, 1) == pdPASS;
}

bool usdtWorkerBusy() {
    return s_busy;
}

bool usdtWorkerRequestData(const UsdtDataSnapshot& seed) {
    UsdtWorkerCommand command = {};
    command.job = USDT_JOB_DATA;
    command.data = seed;
    return request(command);
}

bool usdtWorkerRequestPrice(const UsdtDataSnapshot& seed) {
    UsdtWorkerCommand command = {};
    command.job = USDT_JOB_PRICE;
    command.data = seed;
    return request(command);
}

bool usdtWorkerRequestOtaCheck() {
    UsdtWorkerCommand command = {};
    command.job = USDT_JOB_OTA_CHECK;
    return request(command);
}

bool usdtWorkerRequestOtaProbe() {
    UsdtWorkerCommand command = {};
    command.job = USDT_JOB_OTA_PROBE;
    return request(command);
}

bool usdtWorkerPoll(UsdtWorkerUpdate& update) {
    return s_updates && xQueueReceive(s_updates, &update, 0) == pdTRUE;
}

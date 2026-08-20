#include "usdt_lemon_runtime.h"

#include "api_client.h"
#include "nvs_storage.h"
#include "ota_manager.h"
#include "time_manager.h"
#include "touch_manager.h"
#include "usdt_lemon_data.h"
#include "usdt_lemon_model.h"
#include "usdt_lemon_ui.h"
#include "wifi_manager.h"
#include "wifi_provision.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <esp_task_wdt.h>

namespace {
constexpr uint32_t REFRESH_INTERVAL_MS = 60UL * 1000UL;
constexpr uint32_t CLOCK_REDRAW_MS = 30UL * 1000UL;
constexpr uint32_t OTA_PROBE_MS = 60UL * 1000UL;
constexpr uint32_t OTA_CHECK_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t FETCH_STEP_DELAY_MS = 50UL;

enum UsdtFetchStage : uint8_t {
    USDT_FETCH_IDLE = 0,
    USDT_FETCH_PRICE,
    USDT_FETCH_RATES,
    USDT_FETCH_YIELD,
    USDT_FETCH_VARIATIONS,
};

UsdtRuntimeModel s_model;
UsdtDataSnapshot s_data;
UsdtDeviceInfo s_device;
OtaInfo s_otaInfo = {};
bool s_provisioning = false;
bool s_networkReady = false;
uint32_t s_lastFetchMs = 0;
uint32_t s_lastDrawMs = 0;
uint32_t s_lastOtaCheckMs = 0;
uint32_t s_lastOtaProbeMs = 0;
uint8_t s_lastVariation = 255;
UsdtFetchStage s_fetchStage = USDT_FETCH_IDLE;
uint32_t s_nextFetchStepMs = 0;
bool s_bootOtaPending = false;

void clearCredentials(char* ssid, size_t ssidLen, char* pass, size_t passLen) {
    if (ssid && ssidLen) memset(ssid, 0, ssidLen);
    if (pass && passLen) memset(pass, 0, passLen);
}

void updateDeviceInfo() {
    const bool clockReady = timeReady();
    const char* clock = clockReady ? getTimeStr(true) : "--:--:--";
    if (clockReady) {
        strncpy(s_device.time, clock, sizeof(s_device.time) - 1);
        s_device.time[5] = '\0';
    } else {
        strncpy(s_device.time, clock, sizeof(s_device.time) - 1);
    }
    s_device.time[sizeof(s_device.time) - 1] = '\0';
    const char* ssid = wifiConnected() ? wifiSSID() : "SIN CONEXION";
    strncpy(s_device.ssid, ssid, sizeof(s_device.ssid) - 1);
    s_device.ssid[sizeof(s_device.ssid) - 1] = '\0';
    String ip = wifiConnected() ? wifiIP() : String("--");
    strncpy(s_device.ip, ip.c_str(), sizeof(s_device.ip) - 1);
    s_device.ip[sizeof(s_device.ip) - 1] = '\0';
    s_device.rssi = wifiConnected() ? wifiRSSI() : 0;
    s_device.uptimeSeconds = millis() / 1000UL;
    s_device.freeHeap = ESP.getFreeHeap();
    s_device.provisioning = s_provisioning;
}

void redraw() {
    updateDeviceInfo();
    usdtDataUpdateFreshness(s_data, millis(), wifiConnected());
    usdtUiDraw(s_data, s_model, s_device);
    s_lastDrawMs = millis();
    s_lastVariation = usdtVariationIndex(s_lastDrawMs);
}

void refreshNow() {
    if (!wifiConnected()) {
        redraw();
        return;
    }
    if (s_fetchStage != USDT_FETCH_IDLE) return;
    s_data.fetching = true;
    s_fetchStage = USDT_FETCH_PRICE;
    s_nextFetchStepMs = millis();
    redraw();
}

void installUsdtOtaNow() {
    if (!s_data.ota.available || !s_otaInfo.url[0] || !s_otaInfo.md5[0]) return;
    usdtUiDrawLoading("ACTUALIZANDO FIRMWARE", 8);
    apiStop();
    otaFlash(s_otaInfo.url, nullptr, s_otaInfo.md5);
    s_data.ota.available = false;
    s_networkReady = false;
    apiSetup();
    redraw();
}

void serviceDataFetch() {
    if (s_fetchStage == USDT_FETCH_IDLE || !wifiConnected() ||
        static_cast<int32_t>(millis() - s_nextFetchStepMs) < 0) {
        return;
    }

    switch (s_fetchStage) {
        case USDT_FETCH_PRICE:
            usdtDataFetchPrice(s_data);
            s_fetchStage = USDT_FETCH_RATES;
            break;
        case USDT_FETCH_RATES:
            usdtDataFetchRates(s_data);
            s_fetchStage = USDT_FETCH_YIELD;
            break;
        case USDT_FETCH_YIELD:
            usdtDataFetchYield(s_data);
            s_fetchStage = USDT_FETCH_VARIATIONS;
            break;
        case USDT_FETCH_VARIATIONS:
            usdtDataFetchVariations(s_data);
            s_fetchStage = USDT_FETCH_IDLE;
            s_data.fetching = false;
            s_lastFetchMs = millis();
            break;
        case USDT_FETCH_IDLE:
            return;
    }
    s_nextFetchStepMs = millis() + FETCH_STEP_DELAY_MS;
    redraw();
}

void checkUsdtOtaNow(bool bootCheck) {
    if (!wifiConnected()) return;
    s_data.ota.checking = true;
    if (bootCheck) usdtUiDrawLoading("BUSCANDO ACTUALIZACIONES", 40);
    else redraw();
    s_otaInfo = otaCheckAsset(OTA_GITHUB_REPO, OTA_USDT_ASSET, APP_VERSION);
    if (s_otaInfo.available && !s_otaInfo.md5[0]) {
        Serial.println("[USDT OTA] Ignoring update without channel-specific MD5");
        s_otaInfo.available = false;
        s_otaInfo.url[0] = '\0';
    }
    s_lastOtaCheckMs = millis();
    s_lastOtaProbeMs = s_lastOtaCheckMs;
    s_data.ota.checked = true;
    s_data.ota.checking = false;
    s_data.ota.available = s_otaInfo.available;
    strncpy(s_data.ota.version, s_otaInfo.version, sizeof(s_data.ota.version) - 1);
    s_data.ota.version[sizeof(s_data.ota.version) - 1] = '\0';
    if (s_data.ota.available) {
        installUsdtOtaNow();
        return;
    }
    if (!bootCheck) redraw();
}

void startNetwork() {
    if (s_networkReady || !wifiConnected()) return;
    s_networkReady = true;
    timeSetup();
    apiSetup();
    s_bootOtaPending = true;
    refreshNow();
}

void startProvisioning() {
    s_provisioning = true;
    s_device.provisioning = true;
    provisionStart();
    provisionDrawQR();
}

bool systemProvisioningTap(const TouchEvent& event) {
    return s_model.scene == USDT_SYSTEM && event.gesture == TOUCH_TAP &&
           usdtSystemActionHit(event.x, event.y);
}
}  // namespace

void usdtLemonSetup() {
    usdtUiSetup();
    usdtDataSetup();
    usdtUiDrawLoading("INICIANDO USDt", 8);
    s_model.lastInteractionMs = millis();

    if (!nvsHasWifi()) {
        startProvisioning();
        return;
    }

    char ssid[33] = {};
    char pass[65] = {};
    nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));
    usdtUiDrawLoading("CONECTANDO WI-FI", 24);
    wifiSetup(ssid, pass);
    clearCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (wifiConnected()) {
        startNetwork();
    } else {
        redraw();
    }
}

void usdtLemonLoop() {
    if (s_provisioning) {
        if (!provisionTick()) return;
        char ssid[33] = {};
        char pass[65] = {};
        provisionGetCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
        provisionStop();
        s_provisioning = false;
        usdtUiDrawLoading("PROBANDO WI-FI", 28);
        wifiSetup(ssid, pass);
        if (wifiConnected()) nvsSaveWifi(ssid, pass);
        clearCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
        if (wifiConnected()) {
            startNetwork();
        } else {
            redraw();
        }
        return;
    }

    wifiLoop();
    const bool online = wifiConnected();
    if (online && !s_networkReady) startNetwork();
    if (!online && s_networkReady) {
        s_networkReady = false;
        s_fetchStage = USDT_FETCH_IDLE;
        s_data.fetching = false;
        s_bootOtaPending = false;
        apiStop();
        redraw();
    }

    TouchEvent event = touchLoop();
    if (event.gesture != TOUCH_NONE) {
        if (systemProvisioningTap(event)) {
            startProvisioning();
            return;
        }
        const bool changed = usdtHandleGesture(s_model, event, millis());
        if (s_model.refreshRequested) {
            s_model.refreshRequested = false;
            refreshNow();
        } else if (s_model.otaCheckRequested) {
            s_model.otaCheckRequested = false;
            checkUsdtOtaNow(false);
        } else if (changed) {
            redraw();
        }
    }

    serviceDataFetch();
    if (online && s_bootOtaPending && s_fetchStage == USDT_FETCH_IDLE) {
        s_bootOtaPending = false;
        checkUsdtOtaNow(false);
    }

    const uint32_t nowMs = millis();
    if (usdtApplyTimeout(s_model, nowMs)) redraw();
    if (online && nowMs - s_lastFetchMs >= REFRESH_INTERVAL_MS) refreshNow();
    if (online && s_data.ota.checked && !s_data.ota.checking &&
        !s_data.ota.available && nowMs - s_lastOtaProbeMs >= OTA_PROBE_MS) {
        s_lastOtaProbeMs = nowMs;
        if (otaLatestTagChanged(OTA_GITHUB_REPO, APP_VERSION)) {
            checkUsdtOtaNow(false);
        }
    }
    if (online && s_data.ota.checked && !s_data.ota.checking &&
        !s_data.ota.available && nowMs - s_lastOtaCheckMs >= OTA_CHECK_MS) {
        checkUsdtOtaNow(false);
    }
    const uint8_t variation = usdtVariationIndex(nowMs);
    if (nowMs - s_lastDrawMs >= CLOCK_REDRAW_MS || variation != s_lastVariation) {
        redraw();
    }
    delay(4);
}

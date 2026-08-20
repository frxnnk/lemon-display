#include "usdt_lemon_runtime.h"

#include "api_client.h"
#include "audio_manager.h"
#include "nvs_storage.h"
#include "ota_manager.h"
#include "time_manager.h"
#include "touch_manager.h"
#include "usdt_lemon_data.h"
#include "usdt_lemon_model.h"
#include "usdt_lemon_ui.h"
#include "usdt_lemon_worker.h"
#include "wifi_manager.h"
#include "wifi_provision.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <esp_task_wdt.h>

namespace {
constexpr uint32_t FULL_REFRESH_INTERVAL_MS = 60UL * 1000UL;
constexpr uint32_t PRICE_REFRESH_INTERVAL_MS = 15UL * 1000UL;
constexpr uint32_t CLOCK_REDRAW_MS = 30UL * 1000UL;
constexpr uint32_t LOADING_ANIMATION_MS = 400;
constexpr uint32_t OTA_PROBE_MS = 60UL * 1000UL;
constexpr uint32_t OTA_CHECK_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t OTA_FAILURE_RETRY_MS = 30UL * 60UL * 1000UL;

UsdtRuntimeModel s_model;
UsdtDataSnapshot s_data;
UsdtDeviceInfo s_device;
OtaInfo s_otaInfo = {};
bool s_provisioning = false;
bool s_networkReady = false;
uint32_t s_lastFetchMs = 0;
uint32_t s_lastPriceFetchMs = 0;
uint32_t s_lastDrawMs = 0;
uint32_t s_lastOtaCheckMs = 0;
uint32_t s_lastOtaProbeMs = 0;
uint32_t s_lastOtaFailureMs = 0;
uint8_t s_lastVariation = 255;
bool s_bootOtaPending = false;
bool s_refreshPending = false;
bool s_otaCheckPending = false;
bool s_initialDataComplete = false;

const char* usdtRuntimeCopy(const char* spanish, const char* english) {
    return s_model.language == USDT_LANGUAGE_EN ? english : spanish;
}

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
    const char* ssid = wifiConnected()
        ? wifiSSID()
        : usdtRuntimeCopy("SIN CONEXION", "NO CONNECTION");
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
    if (!timeReady()) {
        s_refreshPending = true;
        return;
    }
    if (usdtWorkerBusy()) {
        s_refreshPending = true;
        return;
    }
    if (usdtWorkerRequestData(s_data)) {
        s_refreshPending = false;
        s_data.fetching = true;
        redraw();
    }
}

void installUsdtOtaNow() {
    if (!s_data.ota.available || !s_otaInfo.url[0] || !s_otaInfo.md5[0]) return;
    usdtUiDrawLoading(usdtRuntimeCopy("ACTUALIZANDO FIRMWARE", "UPDATING FIRMWARE"), 8);
    apiStop();
    const bool installed = otaFlash(s_otaInfo.url, nullptr, s_otaInfo.md5);
    if (installed) return;
    s_data.ota.available = false;
    s_data.ota.failed = true;
    s_lastOtaFailureMs = millis();
    apiSetup();
    redraw();
}

void applyOtaResult(const OtaInfo& result) {
    s_otaInfo = result;
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
    s_data.ota.failed = false;
    strncpy(s_data.ota.version, s_otaInfo.version, sizeof(s_data.ota.version) - 1);
    s_data.ota.version[sizeof(s_data.ota.version) - 1] = '\0';
    if (s_data.ota.available) {
        installUsdtOtaNow();
        return;
    }
}

void serviceWorkerUpdates() {
    UsdtWorkerUpdate update = {};
    bool changed = false;
    while (usdtWorkerPoll(update)) {
        if (update.kind == USDT_WORKER_DATA_PARTIAL ||
            update.kind == USDT_WORKER_DATA_COMPLETE) {
            s_data = update.data;
            if (update.kind == USDT_WORKER_DATA_PARTIAL && s_data.lemon.valid) {
                s_lastPriceFetchMs = millis();
            }
            if (update.kind == USDT_WORKER_DATA_COMPLETE) {
                s_data.fetching = false;
                s_lastFetchMs = millis();
                s_initialDataComplete = true;
            }
            changed = true;
        } else if (update.kind == USDT_WORKER_PRICE_COMPLETE) {
            s_data = update.data;
            s_lastPriceFetchMs = millis();
            changed = true;
        } else if (update.kind == USDT_WORKER_OTA_CHECK) {
            applyOtaResult(update.ota);
            changed = true;
        } else if (update.tagChanged) {
            s_otaCheckPending = true;
        }
    }
    if (changed) redraw();
}

void requestOtaCheck() {
    if (!wifiConnected() || !timeReady() || usdtWorkerBusy()) {
        s_otaCheckPending = true;
        return;
    }
    if (usdtWorkerRequestOtaCheck()) {
        s_otaCheckPending = false;
        s_data.ota.checking = true;
        redraw();
    }
}

void serviceNetworkScheduling(uint32_t nowMs) {
    if (!wifiConnected() || !timeReady()) return;
    const bool otaFailureCoolingDown = s_lastOtaFailureMs != 0 &&
        nowMs - s_lastOtaFailureMs < OTA_FAILURE_RETRY_MS;
    if (s_refreshPending && !usdtWorkerBusy()) {
        refreshNow();
        return;
    }
    if (s_bootOtaPending && s_initialDataComplete && !usdtWorkerBusy()) {
        s_bootOtaPending = false;
        requestOtaCheck();
        return;
    }
    if (s_otaCheckPending && !usdtWorkerBusy()) {
        requestOtaCheck();
        return;
    }
    if (!usdtWorkerBusy() &&
        nowMs - s_lastFetchMs >= FULL_REFRESH_INTERVAL_MS) {
        refreshNow();
        return;
    }
    if (!usdtWorkerBusy() && s_initialDataComplete &&
        nowMs - s_lastPriceFetchMs >= PRICE_REFRESH_INTERVAL_MS) {
        usdtWorkerRequestPrice(s_data);
        return;
    }
    if (!otaFailureCoolingDown && !usdtWorkerBusy() &&
        s_data.ota.checked && !s_data.ota.available &&
        nowMs - s_lastOtaProbeMs >= OTA_PROBE_MS) {
        s_lastOtaProbeMs = nowMs;
        usdtWorkerRequestOtaProbe();
        return;
    }
    if (!otaFailureCoolingDown && !usdtWorkerBusy() &&
        s_data.ota.checked && !s_data.ota.available &&
        nowMs - s_lastOtaCheckMs >= OTA_CHECK_MS) {
        requestOtaCheck();
    }
}

void startNetwork() {
    if (s_networkReady || !wifiConnected()) return;
    s_networkReady = true;
    timeSetup();
    apiSetup();
    usdtWorkerSetup();
    s_data.fetching = true;
    s_bootOtaPending = true;
    s_refreshPending = true;
    s_initialDataComplete = false;
    redraw();
}

void startProvisioning() {
    s_provisioning = true;
    s_device.provisioning = true;
    provisionStart(s_model.language == USDT_LANGUAGE_EN);
    provisionDrawQR(s_model.language == USDT_LANGUAGE_EN);
}

bool handleSystemControl(const TouchEvent& event) {
    if (s_model.scene != USDT_SYSTEM || event.gesture != TOUCH_TAP) return false;
    const bool soundHit = usdtSystemSoundHit(event.x, event.y);
    const bool languageHit = usdtSystemLanguageHit(event.x, event.y);
    const bool wifiHit = usdtSystemWifiHit(event.x, event.y);
    if (!soundHit && !languageHit && !wifiHit) return false;
    s_model.lastInteractionMs = millis();
    if (soundHit) {
        if (s_model.soundEnabled) playTap();
        s_model.soundEnabled = !s_model.soundEnabled;
        audioSetEnabled(s_model.soundEnabled);
        nvsSetSoundEnabled(s_model.soundEnabled);
        if (s_model.soundEnabled) playTap();
        redraw();
        return true;
    }
    if (languageHit) {
        if (s_model.soundEnabled) playTap();
        s_model.language = s_model.language == USDT_LANGUAGE_ES
            ? USDT_LANGUAGE_EN : USDT_LANGUAGE_ES;
        nvsSetUsdtLanguage(static_cast<uint8_t>(s_model.language));
        usdtUiSetLanguage(s_model.language);
        redraw();
        return true;
    }
    if (wifiHit) {
        if (s_model.soundEnabled) playTap();
        startProvisioning();
        return true;
    }
    return false;
}
}  // namespace

void usdtLemonSetup() {
    s_model.soundEnabled = nvsGetSoundEnabled();
    audioSetEnabled(s_model.soundEnabled);
    s_model.language = static_cast<UsdtLanguage>(nvsGetUsdtLanguage());
    usdtUiSetLanguage(s_model.language);
    usdtUiSetup();
    usdtDataSetup();
    usdtUiDrawLoading(usdtRuntimeCopy("INICIANDO USDt", "STARTING USDt"), 8);
    s_model.lastInteractionMs = millis();

    if (!nvsHasWifi()) {
        startProvisioning();
        return;
    }

    char ssid[33] = {};
    char pass[65] = {};
    nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));
    usdtUiDrawLoading(usdtRuntimeCopy("CONECTANDO WI-FI", "CONNECTING WI-FI"), 24);
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
        usdtUiDrawLoading(usdtRuntimeCopy("PROBANDO WI-FI", "TESTING WI-FI"), 28);
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
        s_data.fetching = false;
        s_bootOtaPending = false;
        s_refreshPending = true;
        s_initialDataComplete = false;
        s_lastPriceFetchMs = 0;
        redraw();
    }

    TouchEvent event = touchLoop();
    if (event.gesture != TOUCH_NONE) {
        if (handleSystemControl(event)) return;
        if (event.gesture == TOUCH_TAP && s_model.soundEnabled) playTap();
        const bool changed = usdtHandleGesture(s_model, event, millis());
        if (s_model.refreshRequested) {
            s_model.refreshRequested = false;
            refreshNow();
        } else if (changed) {
            redraw();
        }
    }

    serviceWorkerUpdates();

    const uint32_t nowMs = millis();
    if (usdtApplyTimeout(s_model, nowMs)) redraw();
    serviceNetworkScheduling(nowMs);
    const uint8_t variation = usdtVariationIndex(nowMs);
    if ((s_data.fetching && nowMs - s_lastDrawMs >= LOADING_ANIMATION_MS) ||
        nowMs - s_lastDrawMs >= CLOCK_REDRAW_MS || variation != s_lastVariation) {
        redraw();
    }
    delay(4);
}

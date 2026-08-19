#include "usdt_lemon_runtime.h"

#include "api_client.h"
#include "nvs_storage.h"
#include "time_manager.h"
#include "touch_manager.h"
#include "usdt_lemon_data.h"
#include "usdt_lemon_model.h"
#include "usdt_lemon_ui.h"
#include "wifi_manager.h"
#include "wifi_provision.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <ctime>

namespace {
constexpr uint32_t REFRESH_INTERVAL_MS = 60UL * 1000UL;
constexpr uint32_t CLOCK_REDRAW_MS = 1000UL;

UsdtRuntimeModel s_model;
UsdtDataSnapshot s_data;
UsdtDeviceInfo s_device;
bool s_provisioning = false;
bool s_networkReady = false;
uint32_t s_lastFetchMs = 0;
uint32_t s_lastDrawMs = 0;

void clearCredentials(char* ssid, size_t ssidLen, char* pass, size_t passLen) {
    if (ssid && ssidLen) memset(ssid, 0, ssidLen);
    if (pass && passLen) memset(pass, 0, passLen);
}

void updateDeviceInfo() {
    const char* clock = timeReady() ? getTimeStr(true) : "--:--:--";
    strncpy(s_device.time, clock, sizeof(s_device.time) - 1);
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
    usdtDataUpdateFreshness(s_data, static_cast<uint32_t>(time(nullptr)),
                            wifiConnected());
    usdtUiDraw(s_data, s_model, s_device);
    s_lastDrawMs = millis();
}

void refreshNow() {
    if (!wifiConnected()) {
        redraw();
        return;
    }
    s_data.fetching = true;
    redraw();
    usdtDataFetch(s_data);
    s_lastFetchMs = millis();
    redraw();
}

void startNetwork() {
    if (s_networkReady || !wifiConnected()) return;
    s_networkReady = true;
    usdtUiDrawLoading("SINCRONIZANDO", 42);
    timeSetup();
    apiSetup();
    usdtUiDrawLoading("LEYENDO MERCADO", 72);
    refreshNow();
}

void startProvisioning() {
    s_provisioning = true;
    s_device.provisioning = true;
    provisionStart();
    provisionDrawQR();
}

bool systemProvisioningTap(const TouchEvent& event) {
    return s_model.scene == USDT_SCENE_SYSTEM && event.gesture == TOUCH_TAP &&
           event.x >= 28 && event.x <= 452 && event.y >= 286 && event.y <= 394;
}
}  // namespace

void usdtLemonSetup() {
    usdtUiSetup();
    usdtDataSetup();
    usdtUiDrawLoading("INICIANDO USDt", 8);
    usdtDataLoadCache(s_data);
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
        apiStop();
        redraw();
    }

    TouchEvent event = touchLoop();
    if (event.gesture != TOUCH_NONE) {
        if (systemProvisioningTap(event)) {
            startProvisioning();
            return;
        }
        const bool sceneChanged = usdtHandleGesture(s_model, event, millis());
        if (sceneChanged || s_model.refreshRequested) {
            const bool refresh = s_model.refreshRequested;
            s_model.refreshRequested = false;
            if (refresh) refreshNow();
            else redraw();
        }
    }

    const uint32_t nowMs = millis();
    if (usdtApplyTimeout(s_model, nowMs)) redraw();
    if (online && nowMs - s_lastFetchMs >= REFRESH_INTERVAL_MS) refreshNow();
    if (nowMs - s_lastDrawMs >= CLOCK_REDRAW_MS) redraw();
    delay(4);
}

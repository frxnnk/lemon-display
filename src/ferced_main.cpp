#include <Arduino.h>
#include <ctime>
#include <esp_task_wdt.h>

#include "colors.h"
#include "display_manager.h"
#include "feed_client.h"
#include "ferced_config.h"
#include "nvs_storage.h"
#include "time_manager.h"
#include "touch_manager.h"
#include "ui_ferced.h"
#include "wifi_manager.h"
#include "wifi_provision.h"

enum AppPhase : uint8_t {
    PHASE_PROVISION,
    PHASE_CONNECTING,
    PHASE_RUNNING,
};

static AppPhase s_phase = PHASE_PROVISION;
static uint8_t  s_index = 0;
static uint32_t s_lastRotate = 0;
static uint32_t s_lastFetch = 0;
static uint32_t s_retryMs = FEED_RETRY_MIN_MS;
static uint32_t s_nextRetry = 0;
static bool     s_offline = false;

#define WIFI_RETRY_MIN_MS  5000UL
#define WIFI_RETRY_MAX_MS 60000UL
static uint32_t s_wifiRetryMs = WIFI_RETRY_MIN_MS;
static uint32_t s_nextWifiRetry = 0;

// NTP deja el reloj del sistema en epoch UTC, igual que el campo ts del feed.
// Sin sincronizar devuelve 0 y la UI cae en "recien".
static uint32_t nowEpoch() {
    if (!timeReady()) return 0;
    return (uint32_t)time(nullptr);
}

static void startProvisioning() {
    s_phase = PHASE_PROVISION;
    provisionStart();
    provisionDrawQR();
}

static void drawCurrent() {
    const uint8_t total = feedCount();
    if (total == 0) {
        uiFercedDrawStatus("SIN CONTENIDO", "No llego nada del feed. Tocar para reintentar.");
        return;
    }
    if (s_index >= total) s_index = 0;
    uiFercedDrawItem(feedItem(s_index), s_index, total, nowEpoch(), s_offline);
}

static void refreshFeed() {
    const FeedResult r = feedFetch();
    s_lastFetch = millis();

    if (r == FEED_UPDATED) {
        s_offline = false;
        s_retryMs = FEED_RETRY_MIN_MS;
        s_nextRetry = 0;
        s_index = 0;
        drawCurrent();
        return;
    }

    // Falla de red: se conserva el pool y se reintenta con backoff, en vez de
    // dejar la pantalla vacia.
    s_offline = true;
    s_nextRetry = millis() + s_retryMs;
    s_retryMs = s_retryMs * 2 > FEED_RETRY_MAX_MS ? FEED_RETRY_MAX_MS : s_retryMs * 2;
    drawCurrent();
}

static void enterRunning() {
    s_phase = PHASE_RUNNING;
    timeSetup();
    uiFercedDrawStatus("CONECTADO", "Buscando contenido.");
    refreshFeed();
    s_lastRotate = millis();
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ferced-display ===");

    nvsInit();
    Colors::setTheme(Colors::THEME_DARK);
    displaySetup();
    displaySetupVSync();
    displaySetBrightness(nvsGetBrightness());
    touchSetup();
    uiFercedSetup();

    if (!nvsHasWifi()) {
        uiFercedDrawStatus("CONFIGURAR", "Escanea el codigo para conectar el equipo a tu red.");
        startProvisioning();
        return;
    }

    char ssid[33] = {};
    char pass[65] = {};
    nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));

    uiFercedDrawStatus("CONECTANDO", ssid);
    wifiSetup(ssid, pass);

    if (!wifiConnected()) {
        uiFercedDrawStatus("SIN RED", "No se pudo conectar. Tocar para reconfigurar.");
        s_phase = PHASE_CONNECTING;
        return;
    }
    enterRunning();
}

void loop() {
    esp_task_wdt_reset();

    if (s_phase == PHASE_PROVISION) {
        if (provisionTick()) {
            char ssid[33] = {};
            char pass[65] = {};
            provisionGetCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
            provisionStop();

            uiFercedDrawStatus("PROBANDO", ssid);
            wifiSetup(ssid, pass);
            if (wifiConnected()) {
                nvsSaveWifi(ssid, pass);
                enterRunning();
            } else {
                uiFercedDrawStatus("NO ANDUVO", "Esa red no conecto. Probemos de nuevo.");
                startProvisioning();
            }
        }
        delay(4);
        return;
    }

    if (s_phase == PHASE_CONNECTING) {
        // Reintenta solo. Un aparato de escritorio no puede quedarse esperando
        // que alguien lo toque porque el router tardo en levantar.
        if (touchLoop().gesture == TOUCH_TAP) {
            startProvisioning();
            return;
        }
        const uint32_t nowMs = millis();
        if (nowMs >= s_nextWifiRetry) {
            char ssid[33] = {};
            char pass[65] = {};
            nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));

            char msg[96];
            snprintf(msg, sizeof(msg), "Reintentando con %s.", ssid);
            uiFercedDrawStatus("SIN RED", msg);
            wifiSetup(ssid, pass);

            if (wifiConnected()) {
                s_wifiRetryMs = WIFI_RETRY_MIN_MS;
                enterRunning();
                return;
            }
            uiFercedDrawStatus("SIN RED", "No conecta. Tocar para reconfigurar.");
            s_nextWifiRetry = millis() + s_wifiRetryMs;
            s_wifiRetryMs = s_wifiRetryMs * 2 > WIFI_RETRY_MAX_MS
                                ? WIFI_RETRY_MAX_MS
                                : s_wifiRetryMs * 2;
        }
        delay(20);
        return;
    }

    wifiLoop();
    const uint32_t now = millis();

    if (touchLoop().gesture == TOUCH_TAP) {
        const uint8_t total = feedCount();
        if (total == 0) {
            refreshFeed();
        } else {
            s_index = (s_index + 1) % total;
            drawCurrent();
        }
        s_lastRotate = now;
    }

    if (now - s_lastRotate >= FEED_ROTATE_MS) {
        const uint8_t total = feedCount();
        if (total > 0) {
            s_index = (s_index + 1) % total;
            drawCurrent();
        }
        s_lastRotate = now;
    }

    const bool dueRefresh = now - s_lastFetch >= FEED_REFRESH_MS;
    const bool dueRetry = s_offline && s_nextRetry != 0 && now >= s_nextRetry;
    if (dueRefresh || dueRetry) refreshFeed();

    delay(10);
}


#include <Arduino.h>
#include "config.h"
#include "colors.h"
#include "nvs_storage.h"
#include "app_state.h"
#include "wifi_manager.h"
#include "wifi_provision.h"
#include "time_manager.h"
#include "api_client.h"
#include "display_manager.h"
#include "data/satoshi_fonts.h"
#include "ui_dashboard.h"
#include "ui_components.h"
#include "touch_manager.h"
#include "touch_utils.h"
#include "audio_manager.h"
#include "scheduler.h"
#include "animation.h"

// ── Dashboard state ──
struct DashboardState {
    BtcPrice      btc    = {};
    SparklineData spark   = {};
    LemonPrice    lemon   = {};
    bool online     = false;
    bool wasOffline = false;
    bool liveMode   = false;
};
static DashboardState state;

// ── Selection state ──
static uint8_t selectedPeriod = 2;  // 0=1h, 1=24h, 2=7d

// ── Animators ──
static ValueAnimator btcPriceAnim;
static ValueAnimator lemonBidAnim;
static ValueAnimator lemonAskAnim;
static SparklineAnimator sparkAnim;

// ── Scheduler & task IDs ──
static Scheduler scheduler;
static uint8_t taskClock, taskBtc, taskSparkline, taskLemon;

// ── Double-tap detection for live mode ──
static unsigned long lastTapZ1Ms = 0;
static const unsigned long DOUBLE_TAP_MS = 400;

// ── Forward declarations ──
static void enterDashboard();
static void tryConnectSavedWifi();
static void startProvisioning();
static int periodToDays(uint8_t period);

// ── WiFi failed screen layout ──
#define FAIL_RETRY_Y    280
#define FAIL_RECONF_Y   340
#define FAIL_BTN_W      300
#define FAIL_BTN_H       48
#define FAIL_BTN_X      ((SCREEN_W - FAIL_BTN_W) / 2)

// ── Period to API days mapping ──
static int periodToDays(uint8_t period) {
    switch (period) {
        case 0: return 1;   // 1h — fetch 1 day, use last portion
        case 1: return 1;   // 24h
        case 2: return 7;   // 7d
        default: return 7;
    }
}

// ── Helper: redraw hero with current state ──
static void redrawHero() {
    dashboardDrawBtcHero(state.btc, state.spark, selectedPeriod);
}

// ── Scheduled callbacks ──
static void updateClock() {
    if (timeReady()) {
        dashboardDrawHeader(getTimeStr().c_str(), !state.online, state.liveMode);
    }
}

static void updateBtc() {
    if (!state.online) return;
    ApiResult res;
    if (state.liveMode) {
        res = fetchBtcPriceSimple(state.btc);
    } else {
        res = fetchBtcPrice(state.btc);
    }
    if (res == API_OK) {
        btcPriceAnim.setTarget(state.btc.usd);
        redrawHero();
        if (nvsGetAlertEnabled() && fabsf(state.btc.change1h) >= ALERT_BTC_1H_THRESHOLD_PCT) {
            if (state.btc.change1h > 0) playAlertUp(); else playAlertDown();
        }
    }
}

static void updateSparkline() {
    if (!state.online) return;
    int days = periodToDays(selectedPeriod);
    if (fetchSparkline(state.spark, days) == API_OK) {
        // For 1h: trim to last 12 points from the fetched data
        if (selectedPeriod == 0 && state.spark.count > 12) {
            int offset = state.spark.count - 12;
            state.spark.minVal = 1e12;
            state.spark.maxVal = -1e12;
            for (int i = 0; i < 12; i++) {
                state.spark.points[i] = state.spark.points[offset + i];
                if (state.spark.points[i] < state.spark.minVal) state.spark.minVal = state.spark.points[i];
                if (state.spark.points[i] > state.spark.maxVal) state.spark.maxVal = state.spark.points[i];
            }
            state.spark.count = 12;
        }
        sparkAnim.start(state.spark.count);
        redrawHero();
    }
}

static void updateLemon() {
    if (!state.online) return;
    if (fetchLemonPrice(state.lemon) == API_OK) {
        lemonBidAnim.setTarget(state.lemon.bid);
        lemonAskAnim.setTarget(state.lemon.ask);
        dashboardDrawLemonDollar(state.lemon);
    }
}

// ── Dashboard touch callback ──
static void onDashboardTouch(const TouchEvent& evt, uint8_t zoneId) {
    if (evt.gesture == TOUCH_TAP) {
        if (zoneId == 1) {
            // Check period badge tap
            uint8_t badge = dashboardHitTestPeriodBadge(evt.x, evt.y);
            if (badge != 255 && badge != selectedPeriod) {
                selectedPeriod = badge;
                Serial.printf("[Touch] Period: %s\n", (badge == 0) ? "1h" : (badge == 1) ? "24h" : "7d");
                redrawHero();
                scheduler.forceRun(taskSparkline);
                return;
            }

            // Double-tap detection for live mode
            unsigned long now = millis();
            if (now - lastTapZ1Ms < DOUBLE_TAP_MS) {
                state.liveMode = !state.liveMode;
                Serial.printf("[Touch] Live mode: %s\n", state.liveMode ? "ON" : "OFF");
                if (state.liveMode) {
                    scheduler.enable(taskBtc, true);
                    showToast("Modo LIVE: 10s");
                } else {
                    showToast("Modo normal: 60s");
                }
                dashboardDrawHeader(getTimeStr().c_str(), !state.online, state.liveMode);
                lastTapZ1Ms = 0;
                return;
            }
            lastTapZ1Ms = now;

            // Single tap elsewhere: force refresh BTC + sparkline
            Serial.println("[Touch] Force refresh: BTC + Sparkline");
            dashboardStartFlash(1);
            scheduler.forceRun(taskBtc);
            scheduler.forceRun(taskSparkline);
            return;
        }

        if (zoneId == 2) {
            Serial.println("[Touch] Force refresh: Lemon");
            dashboardStartFlash(2);
            scheduler.forceRun(taskLemon);
            return;
        }

        if (zoneId == 0) {
            updateClock();
        }
    } else if (evt.gesture == TOUCH_LONG_PRESS && zoneId == 0) {
        appSetScreen(SCREEN_SETTINGS);
    }
}

// ── Redraw dashboard ──
static void redrawDashboard() {
    String timeStr = timeReady() ? getTimeStr() : String("--:--:--");
    dashboardDrawAll(timeStr.c_str(),
                     state.btc, state.spark, selectedPeriod,
                     state.lemon, !state.online, state.liveMode);
}

// ── Draw WiFi Failed screen ──
static void drawWifiFailedScreen() {
    tft.fillScreen(Colors::BG_BASE);

    // Icon / title
    tft.setTextColor(Colors::NEGATIVE, Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString("!", SCREEN_W / 2, 120, &SatoshiBold40);

    tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    tft.drawString("No se pudo conectar", SCREEN_W / 2, 180, &SatoshiMedium18);

    // Show which SSID we tried
    char ssid[33];
    char pass[65];
    nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));

    char buf[64];
    snprintf(buf, sizeof(buf), "Red: %s", ssid);
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    tft.drawString(buf, SCREEN_W / 2, 220, &Satoshi12);

    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    tft.drawString("Verifica la contrasena o el router", SCREEN_W / 2, 248, &Satoshi9);

    // Button: Reintentar (green outline)
    tft.fillSmoothRoundRect(FAIL_BTN_X, FAIL_RETRY_Y, FAIL_BTN_W, FAIL_BTN_H, 12, Colors::BG_SURFACE);
    tft.drawRoundRect(FAIL_BTN_X, FAIL_RETRY_Y, FAIL_BTN_W, FAIL_BTN_H, 12, Colors::LEMON_GREEN);
    tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_SURFACE);
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString("Reintentar", FAIL_BTN_X + FAIL_BTN_W / 2, FAIL_RETRY_Y + FAIL_BTN_H / 2, &Satoshi12);

    // Button: Reconfigurar WiFi (red outline)
    tft.fillSmoothRoundRect(FAIL_BTN_X, FAIL_RECONF_Y, FAIL_BTN_W, FAIL_BTN_H, 12, Colors::BG_SURFACE);
    tft.drawRoundRect(FAIL_BTN_X, FAIL_RECONF_Y, FAIL_BTN_W, FAIL_BTN_H, 12, Colors::NEGATIVE);
    tft.setTextColor(Colors::NEGATIVE, Colors::BG_SURFACE);
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString("Reconfigurar WiFi", FAIL_BTN_X + FAIL_BTN_W / 2, FAIL_RECONF_Y + FAIL_BTN_H / 2, &Satoshi12);
}

// ── Try connecting with saved WiFi credentials ──
static void tryConnectSavedWifi() {
    char ssid[33], pass[65];
    nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));

    wifiSetup(ssid, pass);
    state.online = wifiConnected();

    if (state.online) {
        enterDashboard();
    } else {
        Serial.println("[Main] WiFi failed — showing fail screen");
        drawWifiFailedScreen();
        appSetScreen(SCREEN_WIFI_FAILED);
    }
}

// ── Start QR provisioning flow ──
static void startProvisioning() {
    nvsForgetWifi();
    provisionStart();
    provisionDrawQR();
    appSetScreen(SCREEN_WIFI_QR);
}

// ── Enter dashboard: fetch data + draw ──
static void enterDashboard() {
    appSetScreen(SCREEN_LOADING);

    dashboardDrawLoading(LOAD_NTP);
    timeSetup();
    apiSetup();

    unsigned long start = millis();
    while (!timeReady() && millis() - start < 5000) {
        delay(100);
    }

    dashboardDrawLoading(LOAD_DATA);
    Serial.println("[Main] Initial data fetch...");
    fetchBtcPrice(state.btc);

    // Initialize BTC price animator
    btcPriceAnim.set(state.btc.usd);

    dashboardDrawLoading(LOAD_DOLLAR);
    Serial.println("[Main] Fetching Lemon dollar...");
    fetchLemonPrice(state.lemon);
    lemonBidAnim.set(state.lemon.bid);
    lemonAskAnim.set(state.lemon.ask);

    dashboardDrawLoading(LOAD_CHART);
    fetchSparkline(state.spark, periodToDays(selectedPeriod));

    dashboardDrawLoading(LOAD_DONE);
    delay(300);

    String timeStr = getTimeStr();
    dashboardDrawAll(timeStr.c_str(),
                     state.btc, state.spark, selectedPeriod,
                     state.lemon, !state.online, state.liveMode);

    appSetScreen(SCREEN_DASHBOARD);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Lemon Interface v3.1 (MaTouch 480x480) ===");

    nvsInit();

    displaySetup();
    displaySetBrightness(nvsGetBrightness());
    dashboardSetup();
    dashboardDrawLoading(LOAD_LOGO);

    audioSetup();
    audioSetEnabled(nvsGetSoundEnabled());

    touchSetup();
    appInit();

    dashboardSetTouchCallback(onDashboardTouch);
    appSetDashboardRedrawCB(redrawDashboard);

    taskClock     = scheduler.add("clock",     UPDATE_CLOCK_MS,      updateClock);
    taskBtc       = scheduler.add("btc",       UPDATE_BTC_PRICE_MS,  updateBtc);
    taskSparkline = scheduler.add("sparkline", UPDATE_SPARKLINE_MS,  updateSparkline);
    taskLemon     = scheduler.add("lemon",     UPDATE_LEMON_MS,      updateLemon);

    // ── Boot flow ──
    if (nvsHasWifi()) {
        dashboardDrawLoading(LOAD_WIFI);
        tryConnectSavedWifi();
    } else {
        Serial.println("[Main] No WiFi configured — starting QR provisioning");
        startProvisioning();
    }

    playStartup();
    Serial.println("[Main] Ready");
}

void loop() {
    AppScreen screen = appGetScreen();

    // ── WiFi QR Provisioning mode ──
    if (screen == SCREEN_WIFI_QR) {
        if (provisionTick()) {
            char ssid[33], pass[65];
            provisionGetCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
            nvsSaveWifi(ssid, pass);
            provisionStop();

            // Show connecting screen
            tft.fillScreen(Colors::BG_BASE);
            tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
            tft.setTextDatum(lgfx::middle_center);
            tft.drawString("Conectando...", SCREEN_W / 2, SCREEN_H / 2 - 10, &SatoshiMedium18);
            tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
            tft.drawString(ssid, SCREEN_W / 2, SCREEN_H / 2 + 20, &Satoshi12);
            appSetScreen(SCREEN_WIFI_CONNECTING);

            wifiSetup(ssid, pass);
            state.online = wifiConnected();

            if (state.online) {
                enterDashboard();
            } else {
                Serial.println("[Main] Post-provision connect failed");
                drawWifiFailedScreen();
                appSetScreen(SCREEN_WIFI_FAILED);
            }
        }
        TouchEvent evt = touchLoop();
        (void)evt;
        delay(50);
        return;
    }

    // ── WiFi Failed screen — handle touch ──
    if (screen == SCREEN_WIFI_FAILED) {
        TouchEvent evt = touchLoop();
        if (evt.gesture == TOUCH_TAP) {
            // "Reintentar" button
            if (touchInRect(evt.x, evt.y, FAIL_BTN_X, FAIL_RETRY_Y, FAIL_BTN_W, FAIL_BTN_H)) {
                Serial.println("[Main] Retry WiFi connection");
                tryConnectSavedWifi();
            }
            // "Reconfigurar WiFi" button
            else if (touchInRect(evt.x, evt.y, FAIL_BTN_X, FAIL_RECONF_Y, FAIL_BTN_W, FAIL_BTN_H)) {
                Serial.println("[Main] Reconfigure WiFi — starting provisioning");
                startProvisioning();
            }
        }
        delay(20);
        return;
    }

    // ── Dashboard mode ──
    if (screen == SCREEN_DASHBOARD) {
        wifiLoop();
        state.online = wifiConnected();

        if (!state.online && !state.wasOffline) {
            dashboardDrawHeader("--:--:--", true);
            state.wasOffline = true;
            showToast("WiFi desconectado");
        } else if (state.online && state.wasOffline) {
            state.wasOffline = false;
            timeSetup();
            dashboardDrawHeader(getTimeStr().c_str(), false, state.liveMode);
            showToast("WiFi reconectado");
        }

        scheduler.tick();

        // Live mode: poll BTC more frequently (10s instead of 60s)
        if (state.liveMode && state.online) {
            static unsigned long lastLivePoll = 0;
            unsigned long now = millis();
            if (now - lastLivePoll >= UPDATE_BTC_LIVE_MS) {
                lastLivePoll = now;
                updateBtc();
            }
        }

        // Animation frame updates (throttle to ~30fps to reduce tearing)
        {
            static unsigned long lastAnimFrame = 0;
            unsigned long now = millis();
            if (now - lastAnimFrame >= 33) {
                lastAnimFrame = now;
                if (btcPriceAnim.isAnimating() || sparkAnim.isAnimating()) {
                    btcPriceAnim.update();
                    redrawHero();
                }
                if (lemonBidAnim.isAnimating() || lemonAskAnim.isAnimating()) {
                    lemonBidAnim.update();
                    lemonAskAnim.update();
                    dashboardDrawLemonDollar(state.lemon);
                }
            }
        }
    }

    // Per-frame updates
    appTick();
    updateToast();
    dashboardUpdateFlash();

    // Poll touch and dispatch
    TouchEvent evt = touchLoop();
    appHandleTouch(evt);

    delay(20);
}

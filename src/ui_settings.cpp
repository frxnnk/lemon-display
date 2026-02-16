#include "ui_settings.h"
#include "app_state.h"
#include "nvs_storage.h"
#include "wifi_manager.h"
#include "display_manager.h"
#include "ui_components.h"
#include "colors.h"
#include "config.h"
#include "touch_utils.h"
#include "data/satoshi_fonts.h"
#include <Arduino.h>

#define MARGIN     16
#define CARD_W    448
#define CARD_R     16
#define HEADER_H   44

// ── Layout Y positions ──
#define BRIGHT_CARD_Y  (HEADER_H + 8)
#define BRIGHT_CARD_H  100
#define WIFI_CARD_Y    (BRIGHT_CARD_Y + BRIGHT_CARD_H + 8)
#define WIFI_CARD_H    70
#define RESET_BTN_Y    (WIFI_CARD_Y + WIFI_CARD_H + 12)
#define RESET_BTN_H    48
#define ABOUT_Y        (RESET_BTN_Y + RESET_BTN_H + 16)

// ── Slider state ──
static float sliderValue = 1.0f;

static LGFX_Sprite settSpr(&tft);

// ══════════════════════════════════════════
//  SINGLE-PAGE SETTINGS
// ══════════════════════════════════════════

void settingsDraw() {
    sliderValue = nvsGetBrightness() / 255.0f;

    tft.fillScreen(Colors::BG_BASE);

    // ── Header ──
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_left);
    tft.drawString("<", MARGIN, HEADER_H / 2, &SatoshiMedium18);
    tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString("Ajustes", SCREEN_W / 2, HEADER_H / 2, &SatoshiMedium18);

    // ── Brightness Card ──
    settSpr.setColorDepth(16);
    settSpr.createSprite(CARD_W, BRIGHT_CARD_H);
    settSpr.fillSprite(Colors::BG_BASE);
    drawGlassCard(settSpr, 0, 0, CARD_W, BRIGHT_CARD_H, CARD_R);

    settSpr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
    settSpr.setTextDatum(lgfx::top_left);
    settSpr.drawString("Brillo", 16, 14, &Satoshi12);

    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", (int)(sliderValue * 100));
    settSpr.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
    settSpr.setTextDatum(lgfx::top_right);
    settSpr.drawString(pctBuf, CARD_W - 16, 14, &SatoshiMedium18);

    // Slider
    drawSlider(settSpr, 16, 50, CARD_W - 32, 14, sliderValue);

    // Quick buttons
    int btnW = (CARD_W - 32 - 2 * 8) / 3;
    int btnH = 28;
    int btnY = 74;
    const char* labels[] = {"Min", "50%", "Max"};

    for (int i = 0; i < 3; i++) {
        int bx = 16 + i * (btnW + 8);
        settSpr.fillSmoothRoundRect(bx, btnY, btnW, btnH, 8, Colors::BG_ELEVATED);
        settSpr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_ELEVATED);
        settSpr.setTextDatum(lgfx::middle_center);
        settSpr.drawString(labels[i], bx + btnW / 2, btnY + btnH / 2, &Satoshi9);
    }

    settSpr.pushSprite(MARGIN, BRIGHT_CARD_Y);
    settSpr.deleteSprite();

    // ── WiFi Info Card ──
    settSpr.setColorDepth(16);
    settSpr.createSprite(CARD_W, WIFI_CARD_H);
    settSpr.fillSprite(Colors::BG_BASE);
    drawGlassCard(settSpr, 0, 0, CARD_W, WIFI_CARD_H, CARD_R);

    settSpr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
    settSpr.setTextDatum(lgfx::top_left);
    settSpr.drawString("WiFi", 16, 12, &Satoshi9);

    if (wifiConnected()) {
        settSpr.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::top_left);
        settSpr.drawString(wifiSSID(), 16, 28, &Satoshi12);

        char rssiBuf[24];
        snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", (int)wifiRSSI());
        settSpr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::top_right);
        settSpr.drawString(rssiBuf, CARD_W - 16, 12, &Satoshi9);

        String ip = wifiIP();
        settSpr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::top_left);
        char ipBuf[32];
        snprintf(ipBuf, sizeof(ipBuf), "IP: %s", ip.c_str());
        settSpr.drawString(ipBuf, 16, 48, &Satoshi9);
    } else {
        settSpr.setTextColor(Colors::NEGATIVE, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::top_left);
        settSpr.drawString("Desconectado", 16, 28, &Satoshi12);
    }

    settSpr.pushSprite(MARGIN, WIFI_CARD_Y);
    settSpr.deleteSprite();

    // ── Reset WiFi Button ──
    tft.fillSmoothRoundRect(MARGIN, RESET_BTN_Y, CARD_W, RESET_BTN_H, 12, Colors::BG_SURFACE);
    tft.drawRoundRect(MARGIN, RESET_BTN_Y, CARD_W, RESET_BTN_H, 12, Colors::NEGATIVE);
    tft.setTextColor(Colors::NEGATIVE, Colors::BG_SURFACE);
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString("Resetear WiFi", MARGIN + CARD_W / 2, RESET_BTN_Y + RESET_BTN_H / 2, &Satoshi12);

    // ── About (compact) ──
    char verBuf[32];
    snprintf(verBuf, sizeof(verBuf), "v%s | MaTouch ESP32-S3", APP_VERSION);
    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::top_center);
    tft.drawString(verBuf, SCREEN_W / 2, ABOUT_Y, &Satoshi9);

    unsigned long sec = millis() / 1000;
    char uptimeBuf[32];
    snprintf(uptimeBuf, sizeof(uptimeBuf), "Uptime: %luh %lum", sec / 3600, (sec / 60) % 60);
    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    tft.drawString(uptimeBuf, SCREEN_W / 2, ABOUT_Y + 16, &Satoshi9);
}

// ══════════════════════════════════════════
//  TOUCH HANDLING
// ══════════════════════════════════════════

void settingsHandleTouch(const TouchEvent& evt) {
    // Swipe right = back to dashboard
    if (evt.gesture == TOUCH_SWIPE_RIGHT) {
        appSetScreen(SCREEN_DASHBOARD);
        return;
    }

    if (evt.gesture != TOUCH_TAP && evt.gesture != TOUCH_LONG_PRESS) return;

    int tx = evt.x;
    int ty = evt.y;

    // Back arrow
    if (touchInRect(tx, ty, 0, 0, 60, HEADER_H)) {
        appSetScreen(SCREEN_DASHBOARD);
        return;
    }

    // ── Brightness slider ──
    int sliderScreenY = BRIGHT_CARD_Y + 50;
    int sliderX = MARGIN + 16;
    int sliderW = CARD_W - 32;
    if (touchInRect(tx, ty, sliderX - 10, sliderScreenY - 10, sliderW + 20, 34)) {
        sliderValue = (float)(tx - sliderX) / sliderW;
        if (sliderValue < 0.04f) sliderValue = 0.04f;
        if (sliderValue > 1.0f) sliderValue = 1.0f;
        uint8_t br = (uint8_t)(sliderValue * 255);
        nvsSetBrightness(br);
        displaySetBrightness(br);
        settingsDraw();
        return;
    }

    // ── Quick buttons ──
    int qBtnW = (CARD_W - 32 - 2 * 8) / 3;
    int qBtnH = 28;
    int qBtnScreenY = BRIGHT_CARD_Y + 74;
    float vals[] = {0.1f, 0.5f, 1.0f};

    for (int i = 0; i < 3; i++) {
        int bx = MARGIN + 16 + i * (qBtnW + 8);
        if (touchInRect(tx, ty, bx, qBtnScreenY, qBtnW, qBtnH)) {
            sliderValue = vals[i];
            uint8_t br = (uint8_t)(sliderValue * 255);
            nvsSetBrightness(br);
            displaySetBrightness(br);
            settingsDraw();
            return;
        }
    }

    // ── Reset WiFi button ──
    if (touchInRect(tx, ty, MARGIN, RESET_BTN_Y, CARD_W, RESET_BTN_H)) {
        nvsForgetWifi();
        ESP.restart();
        return;
    }
}

void settingsTick() {
    // Reserved for future slider drag
}

#include "ui_settings.h"
#include "app_state.h"
#include "nvs_storage.h"
#include "wifi_manager.h"
#include "display_manager.h"
#include "ui_components.h"
#include "ui_dashboard.h"
#include "ota_manager.h"
#include "supabase_client.h"
#include "colors.h"
#include "config.h"
#include "touch_utils.h"
#include "audio_manager.h"
#include "data/satoshi_fonts.h"
#include <Arduino.h>

#define MARGIN     16
#define CARD_W    448
#define CARD_R     16
#define HEADER_H   44
#define CONTENT_Y  HEADER_H  // Scrollable content starts here
#define VISIBLE_H  (SCREEN_H - HEADER_H)

// ── Content layout (Y positions relative to screen top, before scroll) ──
#define DISPLAY_CARD_Y   50
#define DISPLAY_CARD_H  130
#define AUDIO_CARD_Y    184
#define AUDIO_CARD_H     74
#define LAYOUT_CARD_Y   262
#define LAYOUT_CARD_H    44
#define WIFI_CARD_Y     310
#define WIFI_CARD_H      56
#define LINK_CARD_Y     370
#define LINK_CARD_H      56
#define UPDATE_BTN_Y    432
#define UPDATE_BTN_H     40
#define RESET_BTN_Y     478
#define RESET_BTN_H      40
#define ABOUT_Y         524
#define ABOUT_H          16
#define CONTENT_TOTAL   (ABOUT_Y + ABOUT_H)
#define MAX_SCROLL      ((CONTENT_TOTAL > SCREEN_H) ? (CONTENT_TOTAL - SCREEN_H) : 0)

// ── State ──
static float sliderValue = 1.0f;
static int scrollY = 0;
static bool otaChecked = false;
static OtaInfo otaResult = {};
static bool otaFlashing = false;

static LGFX_Sprite settSpr(&tft);

// ── Layout preset names ──
static const char* layoutNames[] = { "Estandar", "BTC Focus", "Compacto" };

// ── Helper: check if Y range is visible ──
static bool isVisible(int itemY, int itemH) {
    int screenTop = itemY - scrollY;
    int screenBot = screenTop + itemH;
    return (screenBot > CONTENT_Y && screenTop < SCREEN_H);
}

// ══════════════════════════════════════════
//  DRAW
// ══════════════════════════════════════════

void settingsDraw() {
    sliderValue = nvsGetBrightness() / 255.0f;

    tft.fillScreen(Colors::BG_BASE);

    // Clip to scrollable content area
    tft.setClipRect(0, CONTENT_Y, SCREEN_W, VISIBLE_H);

    // ══════════════════════════════════════
    //  DISPLAY CARD (Brillo + Formato hora)
    // ══════════════════════════════════════
    if (isVisible(DISPLAY_CARD_Y, DISPLAY_CARD_H)) {
        int cardScreenY = DISPLAY_CARD_Y - scrollY;

        settSpr.setColorDepth(16);
        settSpr.createSprite(CARD_W, DISPLAY_CARD_H);
        settSpr.fillSprite(Colors::BG_BASE);
        drawGlassCard(settSpr, 0, 0, CARD_W, DISPLAY_CARD_H, CARD_R);

        // "Brillo" label + percentage
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

        // "Formato hora" + toggle
        settSpr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::middle_left);
        settSpr.drawString("Formato hora", 16, 118, &Satoshi12);
        bool is24h = nvsGet24hFormat();
        drawToggle(settSpr, CARD_W - 44 - 16, 106, is24h);
        // Label: "24h" or "12h"
        settSpr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::middle_right);
        settSpr.drawString(is24h ? "24h" : "12h", CARD_W - 44 - 24, 118, &Satoshi9);

        settSpr.pushSprite(MARGIN, cardScreenY);
        settSpr.deleteSprite();
    }

    // ══════════════════════════════════════
    //  AUDIO CARD (Sonido + Alertas BTC)
    // ══════════════════════════════════════
    if (isVisible(AUDIO_CARD_Y, AUDIO_CARD_H)) {
        int cardScreenY = AUDIO_CARD_Y - scrollY;

        settSpr.setColorDepth(16);
        settSpr.createSprite(CARD_W, AUDIO_CARD_H);
        settSpr.fillSprite(Colors::BG_BASE);
        drawGlassCard(settSpr, 0, 0, CARD_W, AUDIO_CARD_H, CARD_R);

        // "Sonido" + toggle
        settSpr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::middle_left);
        settSpr.drawString("Sonido", 16, 24, &Satoshi12);
        drawToggle(settSpr, CARD_W - 44 - 16, 12, nvsGetSoundEnabled());

        // "Alertas BTC" + toggle
        settSpr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::middle_left);
        settSpr.drawString("Alertas BTC", 16, 54, &Satoshi12);
        drawToggle(settSpr, CARD_W - 44 - 16, 42, nvsGetAlertEnabled());

        settSpr.pushSprite(MARGIN, cardScreenY);
        settSpr.deleteSprite();
    }

    // ══════════════════════════════════════
    //  LAYOUT CARD
    // ══════════════════════════════════════
    if (isVisible(LAYOUT_CARD_Y, LAYOUT_CARD_H)) {
        int cardScreenY = LAYOUT_CARD_Y - scrollY;

        settSpr.setColorDepth(16);
        settSpr.createSprite(CARD_W, LAYOUT_CARD_H);
        settSpr.fillSprite(Colors::BG_BASE);
        drawGlassCard(settSpr, 0, 0, CARD_W, LAYOUT_CARD_H, CARD_R);

        settSpr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::middle_left);
        settSpr.drawString("Layout", 16, LAYOUT_CARD_H / 2, &Satoshi12);

        // Current layout name (tappable)
        uint8_t layout = nvsGetLayout();
        settSpr.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::middle_right);
        settSpr.drawString(layoutNames[layout], CARD_W - 16, LAYOUT_CARD_H / 2, &SatoshiMedium18);

        settSpr.pushSprite(MARGIN, cardScreenY);
        settSpr.deleteSprite();
    }

    // ══════════════════════════════════════
    //  WIFI CARD
    // ══════════════════════════════════════
    if (isVisible(WIFI_CARD_Y, WIFI_CARD_H)) {
        int cardScreenY = WIFI_CARD_Y - scrollY;

        settSpr.setColorDepth(16);
        settSpr.createSprite(CARD_W, WIFI_CARD_H);
        settSpr.fillSprite(Colors::BG_BASE);
        drawGlassCard(settSpr, 0, 0, CARD_W, WIFI_CARD_H, CARD_R);

        settSpr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::top_left);
        settSpr.drawString("WiFi", 16, 10, &Satoshi9);

        if (wifiConnected()) {
            settSpr.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
            settSpr.setTextDatum(lgfx::top_left);
            settSpr.drawString(wifiSSID(), 16, 24, &Satoshi12);

            char rssiBuf[24];
            snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", (int)wifiRSSI());
            settSpr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
            settSpr.setTextDatum(lgfx::top_right);
            settSpr.drawString(rssiBuf, CARD_W - 16, 10, &Satoshi9);

            String ip = wifiIP();
            char ipBuf[32];
            snprintf(ipBuf, sizeof(ipBuf), "IP: %s", ip.c_str());
            settSpr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
            settSpr.setTextDatum(lgfx::top_left);
            settSpr.drawString(ipBuf, 16, 40, &Satoshi9);
        } else {
            settSpr.setTextColor(Colors::NEGATIVE, Colors::BG_CARD);
            settSpr.setTextDatum(lgfx::top_left);
            settSpr.drawString("Desconectado", 16, 24, &Satoshi12);
        }

        settSpr.pushSprite(MARGIN, cardScreenY);
        settSpr.deleteSprite();
    }

    // ══════════════════════════════════════
    //  VINCULACIÓN CARD
    // ══════════════════════════════════════
    if (isVisible(LINK_CARD_Y, LINK_CARD_H)) {
        int cardScreenY = LINK_CARD_Y - scrollY;

        settSpr.setColorDepth(16);
        settSpr.createSprite(CARD_W, LINK_CARD_H);
        settSpr.fillSprite(Colors::BG_BASE);
        drawGlassCard(settSpr, 0, 0, CARD_W, LINK_CARD_H, CARD_R);

        settSpr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settSpr.setTextDatum(lgfx::top_left);
        settSpr.drawString("Vinculacion", 16, 10, &Satoshi9);

        if (supabaseGetPairingState() == PAIRING_PAIRED) {
            // Paired: show @tag + Desvincular
            char tagBuf[40];
            snprintf(tagBuf, sizeof(tagBuf), "@%s", supabaseGetLemonTag());
            settSpr.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
            settSpr.setTextDatum(lgfx::top_left);
            settSpr.drawString(tagBuf, 16, 28, &Satoshi12);

            settSpr.setTextColor(Colors::NEGATIVE, Colors::BG_CARD);
            settSpr.setTextDatum(lgfx::top_right);
            settSpr.drawString("Desvincular", CARD_W - 16, 28, &Satoshi12);
        } else if (supabaseGetPairingState() == PAIRING_REGISTERED) {
            // Not paired: show code
            char codeBuf[24];
            snprintf(codeBuf, sizeof(codeBuf), "Codigo: %s", supabaseGetPairingCode());
            settSpr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
            settSpr.setTextDatum(lgfx::top_left);
            settSpr.drawString(codeBuf, 16, 28, &Satoshi12);

            settSpr.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
            settSpr.setTextDatum(lgfx::top_right);
            settSpr.drawString("Vincular con Lemon", CARD_W - 16, 28, &Satoshi12);
        } else {
            // Not registered
            settSpr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
            settSpr.setTextDatum(lgfx::top_left);
            settSpr.drawString("No registrado", 16, 28, &Satoshi12);
        }

        settSpr.pushSprite(MARGIN, cardScreenY);
        settSpr.deleteSprite();
    }

    // ══════════════════════════════════════
    //  UPDATE BUTTON (green outline)
    // ══════════════════════════════════════
    if (isVisible(UPDATE_BTN_Y, UPDATE_BTN_H)) {
        int btnScreenY = UPDATE_BTN_Y - scrollY;
        tft.fillSmoothRoundRect(MARGIN, btnScreenY, CARD_W, UPDATE_BTN_H, 12, Colors::BG_SURFACE);
        tft.drawRoundRect(MARGIN, btnScreenY, CARD_W, UPDATE_BTN_H, 12, Colors::LEMON_GREEN);

        const char* label = "Buscar actualizaciones";
        if (otaChecked && otaResult.available) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Actualizar a v%s", otaResult.version);
            label = buf;
            tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_SURFACE);
            tft.setTextDatum(lgfx::middle_center);
            tft.drawString(label, MARGIN + CARD_W / 2, btnScreenY + UPDATE_BTN_H / 2, &Satoshi12);
        } else if (otaChecked && !otaResult.available) {
            label = "Estas al dia";
            tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_SURFACE);
            tft.setTextDatum(lgfx::middle_center);
            tft.drawString(label, MARGIN + CARD_W / 2, btnScreenY + UPDATE_BTN_H / 2, &Satoshi12);
        } else if (otaFlashing) {
            label = "Actualizando...";
            tft.setTextColor(Colors::SOLAR, Colors::BG_SURFACE);
            tft.setTextDatum(lgfx::middle_center);
            tft.drawString(label, MARGIN + CARD_W / 2, btnScreenY + UPDATE_BTN_H / 2, &Satoshi12);
        } else {
            tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_SURFACE);
            tft.setTextDatum(lgfx::middle_center);
            tft.drawString(label, MARGIN + CARD_W / 2, btnScreenY + UPDATE_BTN_H / 2, &Satoshi12);
        }
    }

    // ══════════════════════════════════════
    //  RESET WIFI BUTTON (red outline)
    // ══════════════════════════════════════
    if (isVisible(RESET_BTN_Y, RESET_BTN_H)) {
        int btnScreenY = RESET_BTN_Y - scrollY;
        tft.fillSmoothRoundRect(MARGIN, btnScreenY, CARD_W, RESET_BTN_H, 12, Colors::BG_SURFACE);
        tft.drawRoundRect(MARGIN, btnScreenY, CARD_W, RESET_BTN_H, 12, Colors::NEGATIVE);
        tft.setTextColor(Colors::NEGATIVE, Colors::BG_SURFACE);
        tft.setTextDatum(lgfx::middle_center);
        tft.drawString("Resetear WiFi", MARGIN + CARD_W / 2, btnScreenY + RESET_BTN_H / 2, &Satoshi12);
    }

    // ══════════════════════════════════════
    //  ABOUT
    // ══════════════════════════════════════
    if (isVisible(ABOUT_Y, ABOUT_H)) {
        int aboutScreenY = ABOUT_Y - scrollY;
        unsigned long sec = millis() / 1000;
        char verBuf[64];
#if PRESS_EDITION
        snprintf(verBuf, sizeof(verBuf), "v%s PRESS EDITION  |  Up %luh%lum", APP_VERSION, sec / 3600, (sec / 60) % 60);
        tft.setTextColor(Colors::SOLAR, Colors::BG_BASE);
#else
        snprintf(verBuf, sizeof(verBuf), "v%s  |  Up %luh%lum", APP_VERSION, sec / 3600, (sec / 60) % 60);
        tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
#endif
        tft.setTextDatum(lgfx::top_center);
        tft.drawString(verBuf, SCREEN_W / 2, aboutScreenY, &Satoshi9);
    }

    // Clear clip rect
    tft.clearClipRect();

    // ══════════════════════════════════════
    //  FIXED HEADER (drawn on top)
    // ══════════════════════════════════════
    tft.fillRect(0, 0, SCREEN_W, HEADER_H, Colors::BG_BASE);
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_left);
    tft.drawString("<", MARGIN, HEADER_H / 2, &SatoshiMedium18);
    tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString("Ajustes", SCREEN_W / 2, HEADER_H / 2, &SatoshiMedium18);
    tft.drawFastHLine(MARGIN, HEADER_H - 1, CARD_W, Colors::DIVIDER);
}

// ══════════════════════════════════════════
//  TOUCH HANDLING
// ══════════════════════════════════════════

void settingsHandleTouch(const TouchEvent& evt) {
    // Swipe right = back to dashboard
    if (evt.gesture == TOUCH_SWIPE_RIGHT) {
        scrollY = 0;
        otaChecked = false;
        appSetScreen(SCREEN_DASHBOARD);
        return;
    }

    // Scroll: swipe up/down
    if (evt.gesture == TOUCH_SWIPE_UP) {
        scrollY += 40;
        if (scrollY > MAX_SCROLL) scrollY = MAX_SCROLL;
        settingsDraw();
        return;
    }
    if (evt.gesture == TOUCH_SWIPE_DOWN) {
        scrollY -= 40;
        if (scrollY < 0) scrollY = 0;
        settingsDraw();
        return;
    }

    if (evt.gesture != TOUCH_TAP && evt.gesture != TOUCH_LONG_PRESS) return;

    int tx = evt.x;
    int ty = evt.y;

    // Back arrow (fixed header)
    if (touchInRect(tx, ty, 0, 0, 60, HEADER_H)) {
        scrollY = 0;
        otaChecked = false;
        appSetScreen(SCREEN_DASHBOARD);
        return;
    }

    // Convert touch Y to content Y (add scrollY)
    int cy = ty + scrollY;

    // ══════════ DISPLAY CARD ══════════

    // Brightness slider
    int sliderContentY = DISPLAY_CARD_Y + 50;
    int sliderX = MARGIN + 16;
    int sliderW = CARD_W - 32;
    if (touchInRect(tx, cy, sliderX - 10, sliderContentY - 10, sliderW + 20, 34)) {
        sliderValue = (float)(tx - sliderX) / sliderW;
        if (sliderValue < 0.04f) sliderValue = 0.04f;
        if (sliderValue > 1.0f) sliderValue = 1.0f;
        uint8_t br = (uint8_t)(sliderValue * 255);
        nvsSetBrightness(br);
        displaySetBrightness(br);
        settingsDraw();
        return;
    }

    // Quick buttons (Min / 50% / Max)
    int qBtnW = (CARD_W - 32 - 2 * 8) / 3;
    int qBtnH = 28;
    int qBtnContentY = DISPLAY_CARD_Y + 74;
    float vals[] = {0.1f, 0.5f, 1.0f};
    for (int i = 0; i < 3; i++) {
        int bx = MARGIN + 16 + i * (qBtnW + 8);
        if (touchInRect(tx, cy, bx, qBtnContentY, qBtnW, qBtnH)) {
            sliderValue = vals[i];
            uint8_t br = (uint8_t)(sliderValue * 255);
            nvsSetBrightness(br);
            displaySetBrightness(br);
            settingsDraw();
            return;
        }
    }

    // Time format toggle (right side of display card, Y+106)
    int toggleX = MARGIN + CARD_W - 44 - 16;
    int toggleY = DISPLAY_CARD_Y + 106;
    if (touchInRect(tx, cy, toggleX - 10, toggleY - 4, 64, 32)) {
        bool cur = nvsGet24hFormat();
        nvsSet24hFormat(!cur);
        settingsDraw();
        return;
    }

    // ══════════ AUDIO CARD ══════════

    // Sound toggle
    int soundToggleX = MARGIN + CARD_W - 44 - 16;
    int soundToggleY = AUDIO_CARD_Y + 12;
    if (touchInRect(tx, cy, soundToggleX - 10, soundToggleY - 4, 64, 32)) {
        bool cur = nvsGetSoundEnabled();
        nvsSetSoundEnabled(!cur);
        audioSetEnabled(!cur);
        settingsDraw();
        return;
    }

    // Alert toggle
    int alertToggleY = AUDIO_CARD_Y + 42;
    if (touchInRect(tx, cy, soundToggleX - 10, alertToggleY - 4, 64, 32)) {
        bool cur = nvsGetAlertEnabled();
        nvsSetAlertEnabled(!cur);
        settingsDraw();
        return;
    }

    // ══════════ LAYOUT CARD ══════════
    if (touchInRect(tx, cy, MARGIN, LAYOUT_CARD_Y, CARD_W, LAYOUT_CARD_H)) {
        uint8_t cur = nvsGetLayout();
        uint8_t next = (cur + 1) % 3;
        nvsSetLayout(next);
        dashboardSetLayout(next);
        settingsDraw();
        return;
    }

    // ══════════ VINCULACIÓN CARD ══════════
    if (touchInRect(tx, cy, MARGIN, LINK_CARD_Y, CARD_W, LINK_CARD_H)) {
        if (supabaseGetPairingState() == PAIRING_PAIRED) {
            // Tap "Desvincular" (right side)
            if (tx > SCREEN_W / 2) {
                supabaseUnpair();
                settingsDraw();
            }
        } else if (supabaseGetPairingState() == PAIRING_REGISTERED) {
            // Tap "Vincular con Lemon" — switch to pairing screen
            appSetScreen(SCREEN_PAIRING);
        }
        return;
    }

    // ══════════ UPDATE BUTTON ══════════
    if (touchInRect(tx, cy, MARGIN, UPDATE_BTN_Y, CARD_W, UPDATE_BTN_H)) {
        if (otaChecked && otaResult.available && !otaFlashing) {
            // Second tap: flash
            otaFlashing = true;
            settingsDraw();
            otaFlash(otaResult.url);
            // If otaFlash fails (doesn't reboot), show error
            otaFlashing = false;
            settingsDraw();
        } else if (!otaChecked) {
            // First tap: check
            otaResult = otaCheck(OTA_GITHUB_REPO);
            otaChecked = true;
            settingsDraw();
        }
        return;
    }

    // ══════════ RESET WIFI BUTTON ══════════
    if (touchInRect(tx, cy, MARGIN, RESET_BTN_Y, CARD_W, RESET_BTN_H)) {
        nvsForgetWifi();
        ESP.restart();
        return;
    }
}

void settingsTick() {
    // Reserved for future use
}

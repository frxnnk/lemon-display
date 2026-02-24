#include "ui_settings.h"
#include "app_state.h"
#include "nvs_storage.h"
#include "wifi_manager.h"
#include "display_manager.h"
#include "ui_components.h"
#include "ui_dashboard.h"
#include "ota_manager.h"
#include "colors.h"
#include "config.h"
#include "touch_utils.h"
#include "tutorial_overlay.h"
#include "data/satoshi_fonts.h"
#include <Arduino.h>

#define MARGIN     16
#define CARD_W    448
#define CARD_R     16
#define HEADER_H   44
#define CONTENT_Y  HEADER_H  // Scrollable content starts here
#define VISIBLE_H  (SCREEN_H - HEADER_H)

// ── Content layout (Y positions relative to screen top, before scroll) ──
// Compacted to fit 480px with minimal/no scroll
#define DISPLAY_CARD_Y   46
#define DISPLAY_CARD_H    64
#define PRO_CARD_Y      116
#define PRO_CARD_H       44
#define LAYOUT_CARD_Y   166
#define LAYOUT_CARD_H    50
#define WIFI_CARD_Y     222
#define WIFI_CARD_H      52
#define UPDATE_BTN_Y    280
#define UPDATE_BTN_H     34
#define RESET_BTN_Y     322
#define RESET_BTN_H      34
#define TUTORIAL_BTN_Y  364
#define TUTORIAL_BTN_H   34
#define ABOUT_Y         406
#define ABOUT_H          14
#define CONTENT_TOTAL   (ABOUT_Y + ABOUT_H)
#define MAX_SCROLL      ((CONTENT_TOTAL > SCREEN_H) ? (CONTENT_TOTAL - SCREEN_H) : 0)

// ── State ──
static int scrollY = 0;
static bool otaChecked = false;
static OtaInfo otaResult = {};
static bool otaFlashing = false;
static int otaProgress = 0;
static bool otaAvailableOnBoot = false;
static bool resetWifiConfirmArmed = false;
static uint32_t resetWifiConfirmUntilMs = 0;
static const uint32_t RESET_WIFI_CONFIRM_TIMEOUT_MS = 5000;

// ── Persistent full-screen sprite (allocated once, no fillScreen flash) ──
static LGFX_Sprite settScr(&tft);
static bool settScrReady = false;

// ── Layout preset names (0=BTC only, 1=BTC+USD 50/50) ──
static const char* layoutNames[] = { "BTC", "BTC + USD" };

// ── Helper: check if Y range is visible ──
static bool isVisible(int itemY, int itemH) {
    int screenTop = itemY - scrollY;
    int screenBot = screenTop + itemH;
    return (screenBot > CONTENT_Y && screenTop < SCREEN_H);
}

static void armResetWifiConfirm() {
    resetWifiConfirmArmed = true;
    resetWifiConfirmUntilMs = millis() + RESET_WIFI_CONFIRM_TIMEOUT_MS;
}

static void clearResetWifiConfirm() {
    resetWifiConfirmArmed = false;
    resetWifiConfirmUntilMs = 0;
}

static bool isResetWifiConfirmActive() {
    return resetWifiConfirmArmed && (millis() < resetWifiConfirmUntilMs);
}

// ── Ensure sprite is allocated ──
static void ensureSprite() {
    if (!settScrReady) {
        settScr.setPsram(true);
        settScr.setColorDepth(16);
        settScr.createSprite(SCREEN_W, SCREEN_H);
        settScrReady = true;
    }
}

// ══════════════════════════════════════════
//  DRAW — all rendering goes to settScr, single pushSprite at end
// ══════════════════════════════════════════

void settingsDraw() {
    ensureSprite();

    settScr.fillSprite(Colors::BG_BASE);

    // Clip to scrollable content area
    settScr.setClipRect(0, CONTENT_Y, SCREEN_W, VISIBLE_H);

    // ══════════════════════════════════════
    //  DISPLAY CARD (Formato hora)
    // ══════════════════════════════════════
    if (isVisible(DISPLAY_CARD_Y, DISPLAY_CARD_H)) {
        int cy = DISPLAY_CARD_Y - scrollY;

        drawGlassCard(settScr, MARGIN, cy, CARD_W, DISPLAY_CARD_H, CARD_R);

        settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::top_left);
        settScr.drawString("Pantalla", MARGIN + 16, cy + 10, &Satoshi12);

        // "Formato hora" + toggle
        settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_left);
        settScr.drawString("Formato hora", MARGIN + 16, cy + DISPLAY_CARD_H / 2 + 8, &Satoshi12);
        bool is24h = nvsGet24hFormat();
        drawToggle(settScr, MARGIN + CARD_W - 44 - 16, cy + (DISPLAY_CARD_H - 24) / 2 + 8, is24h);
        // Label: "24h" or "12h"
        settScr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_right);
        settScr.drawString(is24h ? "24h" : "12h", MARGIN + CARD_W - 44 - 24, cy + DISPLAY_CARD_H / 2 + 8, &Satoshi9);
    }

    // ══════════════════════════════════════
    //  PRO CARD
    // ══════════════════════════════════════
    if (isVisible(PRO_CARD_Y, PRO_CARD_H)) {
        int cy = PRO_CARD_Y - scrollY;

        drawGlassCard(settScr, MARGIN, cy, CARD_W, PRO_CARD_H, CARD_R);

        bool proOn = nvsGetProMode();
        settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_left);
        settScr.drawString("Modo Pro", MARGIN + 16, cy + PRO_CARD_H / 2, &Satoshi12);

        settScr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_left);
        settScr.drawString("Pares y Predicciones", MARGIN + 130, cy + PRO_CARD_H / 2, &Satoshi9);

        drawToggle(settScr, MARGIN + CARD_W - 44 - 16, cy + (PRO_CARD_H - 24) / 2, proOn);
    }

    // ══════════════════════════════════════
    //  LAYOUT CARD
    // ══════════════════════════════════════
    if (isVisible(LAYOUT_CARD_Y, LAYOUT_CARD_H)) {
        int cy = LAYOUT_CARD_Y - scrollY;

        drawGlassCard(settScr, MARGIN, cy, CARD_W, LAYOUT_CARD_H, CARD_R);

        settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_left);
        settScr.drawString("Vista del dashboard", MARGIN + 16, cy + LAYOUT_CARD_H / 2, &Satoshi12);

        // Current layout name (tappable)
        uint8_t layout = nvsGetLayout();
        settScr.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_right);
        settScr.drawString(layoutNames[layout], MARGIN + CARD_W - 16, cy + LAYOUT_CARD_H / 2, &SatoshiMedium18);
    }

    // ══════════════════════════════════════
    //  WIFI CARD
    // ══════════════════════════════════════
    if (isVisible(WIFI_CARD_Y, WIFI_CARD_H)) {
        int cy = WIFI_CARD_Y - scrollY;

        drawGlassCard(settScr, MARGIN, cy, CARD_W, WIFI_CARD_H, CARD_R);

        settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::top_left);
        settScr.drawString("WiFi", MARGIN + 16, cy + 8, &Satoshi9);

        if (wifiConnected()) {
            settScr.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
            settScr.setTextDatum(lgfx::top_left);
            settScr.drawString(wifiSSID(), MARGIN + 16, cy + 20, &Satoshi12);

            char rssiBuf[24];
            snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", (int)wifiRSSI());
            settScr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
            settScr.setTextDatum(lgfx::top_right);
            settScr.drawString(rssiBuf, MARGIN + CARD_W - 16, cy + 8, &Satoshi9);
        } else {
            settScr.setTextColor(Colors::NEGATIVE, Colors::BG_CARD);
            settScr.setTextDatum(lgfx::top_left);
            settScr.drawString("Desconectado", MARGIN + 16, cy + 20, &Satoshi12);
        }
    }

    // ══════════════════════════════════════
    //  UPDATE BUTTON (green outline)
    // ══════════════════════════════════════
    if (isVisible(UPDATE_BTN_Y, UPDATE_BTN_H)) {
        int by = UPDATE_BTN_Y - scrollY;
        settScr.fillSmoothRoundRect(MARGIN, by, CARD_W, UPDATE_BTN_H, 12, Colors::BG_SURFACE);
        settScr.drawRoundRect(MARGIN, by, CARD_W, UPDATE_BTN_H, 12, Colors::LEMON_GREEN);

        if (otaFlashing) {
            settScr.setTextColor(Colors::SOLAR, Colors::BG_SURFACE);
            settScr.setTextDatum(lgfx::middle_center);
            settScr.drawString("Actualizando...", MARGIN + CARD_W / 2, by + UPDATE_BTN_H / 2, &Satoshi12);
        } else if (otaChecked && otaResult.available) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Actualizar a v%s", otaResult.version);
            settScr.setTextColor(Colors::LEMON_GREEN, Colors::BG_SURFACE);
            settScr.setTextDatum(lgfx::middle_center);
            settScr.drawString(buf, MARGIN + CARD_W / 2, by + UPDATE_BTN_H / 2, &Satoshi12);
        } else if (otaChecked && !otaResult.available) {
            char statusBuf[48];
            if (otaResult.httpCode != 200) {
                snprintf(statusBuf, sizeof(statusBuf), "Error HTTP %d", otaResult.httpCode);
                settScr.setTextColor(Colors::NEGATIVE, Colors::BG_SURFACE);
            } else {
                snprintf(statusBuf, sizeof(statusBuf), "Estas al dia");
                settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_SURFACE);
            }
            settScr.setTextDatum(lgfx::middle_center);
            settScr.drawString(statusBuf, MARGIN + CARD_W / 2, by + UPDATE_BTN_H / 2, &Satoshi12);
        } else if (otaAvailableOnBoot && !otaChecked) {
            // Boot check found an update — show green dot + text
            settScr.fillCircle(MARGIN + 20, by + UPDATE_BTN_H / 2, 4, Colors::LEMON_GREEN);
            settScr.setTextColor(Colors::LEMON_GREEN, Colors::BG_SURFACE);
            settScr.setTextDatum(lgfx::middle_center);
            settScr.drawString("Actualizacion disponible", MARGIN + CARD_W / 2, by + UPDATE_BTN_H / 2, &Satoshi12);
        } else {
            settScr.setTextColor(Colors::LEMON_GREEN, Colors::BG_SURFACE);
            settScr.setTextDatum(lgfx::middle_center);
            settScr.drawString("Buscar actualizaciones", MARGIN + CARD_W / 2, by + UPDATE_BTN_H / 2, &Satoshi12);
        }
    }

    // ══════════════════════════════════════
    //  RESET WIFI BUTTON (red outline)
    // ══════════════════════════════════════
    if (isVisible(RESET_BTN_Y, RESET_BTN_H)) {
        int by = RESET_BTN_Y - scrollY;
        settScr.fillSmoothRoundRect(MARGIN, by, CARD_W, RESET_BTN_H, 12, Colors::BG_SURFACE);
        bool confirmActive = isResetWifiConfirmActive();
        uint16_t borderColor = confirmActive ? Colors::SOLAR : Colors::NEGATIVE;
        uint16_t textColor = confirmActive ? Colors::SOLAR : Colors::NEGATIVE;
        settScr.drawRoundRect(MARGIN, by, CARD_W, RESET_BTN_H, 12, borderColor);
        settScr.setTextColor(textColor, Colors::BG_SURFACE);
        settScr.setTextDatum(lgfx::middle_center);
        settScr.drawString(confirmActive ? "Confirmar reset WiFi" : "Resetear WiFi",
                           MARGIN + CARD_W / 2, by + RESET_BTN_H / 2, &Satoshi12);
    }

    // ══════════════════════════════════════
    //  TUTORIAL BUTTON (neutral outline)
    // ══════════════════════════════════════
    if (isVisible(TUTORIAL_BTN_Y, TUTORIAL_BTN_H)) {
        int by = TUTORIAL_BTN_Y - scrollY;
        settScr.fillSmoothRoundRect(MARGIN, by, CARD_W, TUTORIAL_BTN_H, 12, Colors::BG_SURFACE);
        settScr.drawRoundRect(MARGIN, by, CARD_W, TUTORIAL_BTN_H, 12, Colors::TEXT_SECONDARY);
        settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_SURFACE);
        settScr.setTextDatum(lgfx::middle_center);
        settScr.drawString("Ver tutorial", MARGIN + CARD_W / 2, by + TUTORIAL_BTN_H / 2, &Satoshi12);
    }

    // ══════════════════════════════════════
    //  ABOUT
    // ══════════════════════════════════════
    if (isVisible(ABOUT_Y, ABOUT_H)) {
        int ay = ABOUT_Y - scrollY;
        unsigned long sec = millis() / 1000;
        char verBuf[48];
        snprintf(verBuf, sizeof(verBuf), "v%s  |  Up %luh%lum", APP_VERSION, sec / 3600, (sec / 60) % 60);
        settScr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
        settScr.setTextDatum(lgfx::top_center);
        settScr.drawString(verBuf, SCREEN_W / 2, ay, &Satoshi9);
    }

    // Clear clip rect
    settScr.clearClipRect();

    // ══════════════════════════════════════
    //  FIXED HEADER (drawn on top, outside clip)
    // ══════════════════════════════════════
    settScr.fillRect(0, 0, SCREEN_W, HEADER_H, Colors::BG_BASE);
    settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    settScr.setTextDatum(lgfx::middle_left);
    settScr.drawString("<", MARGIN, HEADER_H / 2, &SatoshiMedium18);
    settScr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    settScr.setTextDatum(lgfx::middle_center);
    settScr.drawString("Ajustes", SCREEN_W / 2, HEADER_H / 2, &SatoshiMedium18);
    settScr.drawFastHLine(MARGIN, HEADER_H - 1, CARD_W, Colors::DIVIDER);

    // ── Single atomic push — no flicker ──
    displayWaitVSync();
    settScr.pushSprite(0, 0);
}

// ══════════════════════════════════════════
//  OTA PROGRESS SCREEN (direct tft draws, no sprite)
// ══════════════════════════════════════════

static void otaDrawProgress(int pct) {
    // Draw between flash writes — DMA safe at this moment
    const int barW = 300, barH = 18, barR = 9;
    const int cx = SCREEN_W / 2;
    const int barX = cx - barW / 2;
    const int barY = SCREEN_H / 2;
    const int fillW = (int)((float)(barW - 4) * pct / 100.0f);

    tft.fillScreen(0);

    // Title
    tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_center);
    if (pct >= 100)
        tft.drawString("Reiniciando...", cx, barY - 30, &SatoshiMedium18);
    else
        tft.drawString("Actualizando...", cx, barY - 30, &SatoshiMedium18);

    // Bar outline
    tft.drawRoundRect(barX, barY, barW, barH, barR, Colors::MOON);

    // Bar fill
    if (fillW > 0) {
        tft.fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, barR - 2, Colors::LEMON_GREEN);
    }

    // Percentage
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    tft.drawString(buf, cx, barY + barH + 18, &Satoshi12);

    // Version
    char verBuf[32];
    snprintf(verBuf, sizeof(verBuf), "v%s", otaResult.version);
    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    tft.drawString(verBuf, cx, barY + barH + 36, &Satoshi9);
}

// ══════════════════════════════════════════
//  TOUCH HANDLING
// ══════════════════════════════════════════

void settingsHandleTouch(const TouchEvent& evt) {
    // Swipe right = back to dashboard
    if (evt.gesture == TOUCH_SWIPE_RIGHT) {
        scrollY = 0;
        otaChecked = false;
        clearResetWifiConfirm();
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
        clearResetWifiConfirm();
        appSetScreen(SCREEN_DASHBOARD);
        return;
    }

    // Convert touch Y to content Y (add scrollY)
    int cy = ty + scrollY;

    // ══════════ DISPLAY CARD ══════════

    // Time format toggle (right side of display card)
    int toggleX = MARGIN + CARD_W - 44 - 16;
    int toggleY = DISPLAY_CARD_Y + (DISPLAY_CARD_H - 24) / 2 + 8;
    if (touchInRect(tx, cy, toggleX - 10, toggleY - 4, 64, 32)) {
        bool cur = nvsGet24hFormat();
        nvsSet24hFormat(!cur);
        settingsDraw();
        return;
    }

    // ══════════ PRO CARD ══════════
    // ══════════ LAYOUT CARD ══════════
    int proToggleX = MARGIN + CARD_W - 44 - 16;
    int proToggleY = PRO_CARD_Y + (PRO_CARD_H - 24) / 2;
    if (touchInRect(tx, cy, proToggleX - 10, proToggleY - 4, 64, 32)) {
        bool cur = nvsGetProMode();
        nvsSetProMode(!cur);
        if (!cur && !nvsGetProTutDone()) {
            // Turned ON for first time — auto-navigate to dashboard + start tutorial
            tutorialStartPro();
            scrollY = 0;
            otaChecked = false;
            clearResetWifiConfirm();
            appSetScreen(SCREEN_DASHBOARD);
            dashboardMarkAllDirty();
            return;
        }
        settingsDraw();
        return;
    }

    if (touchInRect(tx, cy, MARGIN, LAYOUT_CARD_Y, CARD_W, LAYOUT_CARD_H)) {
        uint8_t cur = nvsGetLayout();
        uint8_t next = (cur + 1) % 2;
        nvsSetLayout(next);
        dashboardSetLayout(next);
        settingsDraw();
        return;
    }

    // ══════════ UPDATE BUTTON ══════════
    if (touchInRect(tx, cy, MARGIN, UPDATE_BTN_Y, CARD_W, UPDATE_BTN_H)) {
        if (otaChecked && otaResult.available && !otaFlashing) {
            otaFlashing = true;

            // Free PSRAM sprite — less memory pressure for TLS + progress draws
            if (settScrReady) {
                settScr.deleteSprite();
                settScrReady = false;
            }

            // Draw initial OTA progress screen (0%)
            displaySetBrightness(80);
            otaDrawProgress(0);

            otaFlash(otaResult.url, otaDrawProgress);
            // If we get here, OTA failed (success reboots)
            displaySetBrightness(255);
            ensureSprite();
            otaFlashing = false;
            settingsDraw();
        } else if (!otaChecked) {
            // First tap: check (use boot result if available)
            if (otaAvailableOnBoot) {
                otaResult = otaCheck(OTA_GITHUB_REPO);
            } else {
                otaResult = otaCheck(OTA_GITHUB_REPO);
            }
            otaChecked = true;
            settingsDraw();
        }
        return;
    }

    // ══════════ RESET WIFI BUTTON ══════════
    if (touchInRect(tx, cy, MARGIN, RESET_BTN_Y, CARD_W, RESET_BTN_H)) {
        if (isResetWifiConfirmActive()) {
            clearResetWifiConfirm();
            nvsForgetWifi();
            ESP.restart();
        } else {
            armResetWifiConfirm();
            settingsDraw();
        }
        return;
    }

    // ══════════ TUTORIAL BUTTON ══════════
    if (touchInRect(tx, cy, MARGIN, TUTORIAL_BTN_Y, CARD_W, TUTORIAL_BTN_H)) {
        tutorialStart();
        scrollY = 0;
        otaChecked = false;
        clearResetWifiConfirm();
        appSetScreen(SCREEN_DASHBOARD);
        dashboardMarkAllDirty();
        return;
    }
}

void settingsTick() {
    if (resetWifiConfirmArmed && !isResetWifiConfirmActive()) {
        clearResetWifiConfirm();
        settingsDraw();
    }
}

void settingsSetOtaAvailable(bool available) {
    otaAvailableOnBoot = available;
}

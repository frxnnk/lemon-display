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
#include "audio_manager.h"
#include "ws_binance.h"
#include "api_client.h"
#include "data/satoshi_fonts.h"
#include <Arduino.h>

#define MARGIN     16
#define CARD_W    448
#define CARD_R     16
#define HEADER_H   44
#define CONTENT_Y  HEADER_H  // Scrollable content starts here
#define VISIBLE_H  (SCREEN_H - HEADER_H)

// ── Content layout: merged settings card + button group ──
#define SCARD_Y         52
#define ROW_H           48
#define SCARD_H        (ROW_H * 5)   // 240px — 5 rows
#define BTN_H           38
#define BTN_GAP         10
#define BTN_START_Y    (SCARD_Y + SCARD_H + 20)              // 264
#define UPDATE_BTN_Y    BTN_START_Y                            // 264
#define UPDATE_BTN_H    BTN_H
#define RESET_BTN_Y    (BTN_START_Y + BTN_H + BTN_GAP)        // 312
#define RESET_BTN_H     BTN_H
#define TUTORIAL_BTN_Y (BTN_START_Y + 2 * (BTN_H + BTN_GAP))  // 360
#define TUTORIAL_BTN_H  BTN_H
#define ABOUT_Y        (TUTORIAL_BTN_Y + BTN_H + 16)           // 414
#define ABOUT_H         14
#define CONTENT_TOTAL  (ABOUT_Y + ABOUT_H)                     // 428
#define MAX_SCROLL     ((CONTENT_TOTAL > SCREEN_H) ? (CONTENT_TOTAL - SCREEN_H) : 0)

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

    // ══════════════════════════════════════
    //  MERGED SETTINGS CARD (4 rows + dividers)
    // ══════════════════════════════════════
    int cy = SCARD_Y - scrollY;
    drawGlassCard(settScr, MARGIN, cy, CARD_W, SCARD_H, CARD_R);

    const int PAD = 16;               // Internal card padding
    const int toggleX = MARGIN + CARD_W - 44 - PAD;  // 404

    // ── Row 0: Formato hora ──
    {
        int rcy = cy + ROW_H / 2;
        settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_left);
        settScr.drawString("Formato hora", MARGIN + PAD, rcy, &Satoshi12);

        bool is24h = nvsGet24hFormat();
        settScr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_right);
        settScr.drawString(is24h ? "24h" : "12h", toggleX - 8, rcy, &Satoshi9);
        drawToggle(settScr, toggleX, rcy - 12, is24h);
    }

    // Divider
    settScr.drawFastHLine(MARGIN + PAD, cy + ROW_H, CARD_W - PAD * 2, Colors::DIVIDER);

    // ── Row 1: Modo Pro ──
    {
        int rcy = cy + ROW_H + ROW_H / 2;
        bool proOn = nvsGetProMode();

        settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_left);
        settScr.drawString("Modo Pro", MARGIN + PAD, rcy, &Satoshi12);

        settScr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_left);
        settScr.drawString("Pares y Predicciones", MARGIN + 130, rcy, &Satoshi9);

        drawToggle(settScr, toggleX, rcy - 12, proOn);
    }

    // Divider
    settScr.drawFastHLine(MARGIN + PAD, cy + ROW_H * 2, CARD_W - PAD * 2, Colors::DIVIDER);

    // ── Row 2: Vista dashboard ──
    {
        int rcy = cy + ROW_H * 2 + ROW_H / 2;

        settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_left);
        settScr.drawString("Vista del dashboard", MARGIN + PAD, rcy, &Satoshi12);

        uint8_t layout = nvsGetLayout();
        settScr.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_right);
        settScr.drawString(layoutNames[layout], MARGIN + CARD_W - PAD, rcy, &SatoshiMedium18);
    }

    // Divider
    settScr.drawFastHLine(MARGIN + PAD, cy + ROW_H * 3, CARD_W - PAD * 2, Colors::DIVIDER);

    // ── Row 3: WiFi ──
    {
        int rcy = cy + ROW_H * 3 + ROW_H / 2;

        if (wifiConnected()) {
            settScr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
            settScr.setTextDatum(lgfx::middle_left);
            settScr.drawString("WiFi", MARGIN + PAD, rcy - 10, &Satoshi9);

            settScr.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
            settScr.setTextDatum(lgfx::middle_left);
            settScr.drawString(wifiSSID(), MARGIN + PAD, rcy + 8, &Satoshi12);

            char rssiBuf[24];
            snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", (int)wifiRSSI());
            settScr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
            settScr.setTextDatum(lgfx::middle_right);
            settScr.drawString(rssiBuf, MARGIN + CARD_W - PAD, rcy, &Satoshi9);
        } else {
            settScr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
            settScr.setTextDatum(lgfx::middle_left);
            settScr.drawString("WiFi", MARGIN + PAD, rcy - 10, &Satoshi9);

            settScr.setTextColor(Colors::NEGATIVE, Colors::BG_CARD);
            settScr.setTextDatum(lgfx::middle_left);
            settScr.drawString("Desconectado", MARGIN + PAD, rcy + 8, &Satoshi12);
        }
    }

    // Divider
    settScr.drawFastHLine(MARGIN + PAD, cy + ROW_H * 4, CARD_W - PAD * 2, Colors::DIVIDER);

    // ── Row 4: Sonido ──
    {
        int rcy = cy + ROW_H * 4 + ROW_H / 2;
        bool soundOn = audioIsEnabled();

        settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        settScr.setTextDatum(lgfx::middle_left);
        settScr.drawString("Sonido", MARGIN + PAD, rcy, &Satoshi12);

        drawToggle(settScr, toggleX, rcy - 12, soundOn);
    }

    // ══════════════════════════════════════
    //  BUTTONS
    // ══════════════════════════════════════

    // ── Update button (green outline) ──
    {
        int by = UPDATE_BTN_Y - scrollY;
        settScr.fillSmoothRoundRect(MARGIN, by, CARD_W, BTN_H, 12, Colors::BG_SURFACE);
        settScr.drawRoundRect(MARGIN, by, CARD_W, BTN_H, 12, Colors::LEMON_GREEN);

        if (otaFlashing) {
            settScr.setTextColor(Colors::SOLAR, Colors::BG_SURFACE);
            settScr.setTextDatum(lgfx::middle_center);
            settScr.drawString("Actualizando...", MARGIN + CARD_W / 2, by + BTN_H / 2, &Satoshi12);
        } else if (otaChecked && otaResult.available) {
            // Filled green button when update is available
            settScr.fillSmoothRoundRect(MARGIN, by, CARD_W, BTN_H, 12, Colors::DARK_GREEN);
            settScr.drawRoundRect(MARGIN, by, CARD_W, BTN_H, 12, Colors::LEMON_GREEN);
            char buf[48];
            snprintf(buf, sizeof(buf), "Actualizar a v%s", otaResult.version);
            settScr.setTextColor(Colors::TEXT_PRIMARY, Colors::DARK_GREEN);
            settScr.setTextDatum(lgfx::middle_center);
            settScr.drawString(buf, MARGIN + CARD_W / 2, by + BTN_H / 2, &SatoshiMedium18);
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
            settScr.drawString(statusBuf, MARGIN + CARD_W / 2, by + BTN_H / 2, &Satoshi12);
        } else if (otaAvailableOnBoot && !otaChecked) {
            settScr.fillCircle(MARGIN + 20, by + BTN_H / 2, 4, Colors::LEMON_GREEN);
            settScr.setTextColor(Colors::LEMON_GREEN, Colors::BG_SURFACE);
            settScr.setTextDatum(lgfx::middle_center);
            settScr.drawString("Actualizacion disponible", MARGIN + CARD_W / 2, by + BTN_H / 2, &Satoshi12);
        } else {
            settScr.setTextColor(Colors::LEMON_GREEN, Colors::BG_SURFACE);
            settScr.setTextDatum(lgfx::middle_center);
            settScr.drawString("Buscar actualizaciones", MARGIN + CARD_W / 2, by + BTN_H / 2, &Satoshi12);
        }
    }

    // ── Factory reset button (red outline) ──
    {
        int by = RESET_BTN_Y - scrollY;
        settScr.fillSmoothRoundRect(MARGIN, by, CARD_W, BTN_H, 12, Colors::BG_SURFACE);
        bool confirmActive = isResetWifiConfirmActive();
        uint16_t borderColor = confirmActive ? Colors::SOLAR : Colors::NEGATIVE;
        uint16_t textColor = confirmActive ? Colors::SOLAR : Colors::NEGATIVE;
        settScr.drawRoundRect(MARGIN, by, CARD_W, BTN_H, 12, borderColor);
        settScr.setTextColor(textColor, Colors::BG_SURFACE);
        settScr.setTextDatum(lgfx::middle_center);
        settScr.drawString(confirmActive ? "Confirmar reset" : "Reset de fabrica",
                           MARGIN + CARD_W / 2, by + BTN_H / 2, &Satoshi12);
    }

    // ── Tutorial button (neutral outline) ──
    {
        int by = TUTORIAL_BTN_Y - scrollY;
        settScr.fillSmoothRoundRect(MARGIN, by, CARD_W, BTN_H, 12, Colors::BG_SURFACE);
        settScr.drawRoundRect(MARGIN, by, CARD_W, BTN_H, 12, Colors::TEXT_TERTIARY);
        settScr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_SURFACE);
        settScr.setTextDatum(lgfx::middle_center);
        settScr.drawString("Ver tutorial", MARGIN + CARD_W / 2, by + BTN_H / 2, &Satoshi12);
    }

    // ══════════════════════════════════════
    //  ABOUT
    // ══════════════════════════════════════
    {
        int ay = ABOUT_Y - scrollY;
        unsigned long sec = millis() / 1000;
        char verBuf[48];
        snprintf(verBuf, sizeof(verBuf), "v%s  |  Up %luh%lum", APP_VERSION, sec / 3600, (sec / 60) % 60);
        settScr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
        settScr.setTextDatum(lgfx::top_center);
        settScr.drawString(verBuf, SCREEN_W / 2, ay, &Satoshi9);
    }

    // ══════════════════════════════════════
    //  FIXED HEADER (drawn on top)
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

// OTA progress: backlight OFF during flash (MSPI/DMA contention makes
// live UI impossible). Only log to Serial.
static void otaProgressSerial(int pct) {
    // No framebuffer writes — DMA reads black pixels, no corruption
    Serial.printf("[OTA] %d%%\n", pct);
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

    // ══════════ ROW 0: Formato hora (full row tap) ══════════
    if (touchInRect(tx, cy, MARGIN, SCARD_Y, CARD_W, ROW_H)) {
        nvsSet24hFormat(!nvsGet24hFormat());
        settingsDraw();
        return;
    }

    // ══════════ ROW 1: Modo Pro (full row tap) ══════════
    if (touchInRect(tx, cy, MARGIN, SCARD_Y + ROW_H, CARD_W, ROW_H)) {
        bool cur = nvsGetProMode();
        nvsSetProMode(!cur);
        if (!cur && !nvsGetProTutDone()) {
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

    // ══════════ ROW 2: Layout (full row tap) ══════════
    if (touchInRect(tx, cy, MARGIN, SCARD_Y + ROW_H * 2, CARD_W, ROW_H)) {
        uint8_t cur = nvsGetLayout();
        uint8_t next = (cur + 1) % 2;
        nvsSetLayout(next);
        dashboardSetLayout(next);
        settingsDraw();
        return;
    }

    // ══════════ ROW 4: Sonido (full row tap) ══════════
    if (touchInRect(tx, cy, MARGIN, SCARD_Y + ROW_H * 4, CARD_W, ROW_H)) {
        bool cur = audioIsEnabled();
        audioSetEnabled(!cur);
        nvsSetSoundEnabled(!cur);
        settingsDraw();
        return;
    }

    // ══════════ UPDATE BUTTON ══════════
    if (touchInRect(tx, cy, MARGIN, UPDATE_BTN_Y, CARD_W, BTN_H)) {
        if (otaChecked && otaResult.available && !otaFlashing) {
            otaFlashing = true;
            settingsDraw();  // Show "Actualizando..." on the button
            delay(1500);     // Let user read the message

            // Free everything possible before OTA — TLS needs ~50KB heap
            wsBinanceStop();          // Close WebSocket + its TLS session
            apiStop();                // Release API TLS session
            if (settScrReady) {
                settScr.deleteSprite();
                settScrReady = false;
            }

            tft.fillScreen(0);
            delay(100);

            otaFlash(otaResult.url, otaProgressSerial, otaResult.md5);
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

    // ══════════ FACTORY RESET BUTTON ══════════
    if (touchInRect(tx, cy, MARGIN, RESET_BTN_Y, CARD_W, RESET_BTN_H)) {
        if (isResetWifiConfirmActive()) {
            clearResetWifiConfirm();
            nvsFactoryReset();  // Clears ALL NVS: WiFi, tutorial, stats, settings
            ESP.restart();      // Reboots → shows tutorial + WiFi provisioning
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

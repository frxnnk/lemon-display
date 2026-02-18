#include "pairing_screen.h"
#include "supabase_client.h"
#include "app_state.h"
#include "display_manager.h"
#include "ui_components.h"
#include "colors.h"
#include "config.h"
#include "touch_utils.h"
#include "data/satoshi_fonts.h"
#include "data/lemon_logo.h"
#include <Arduino.h>

// ── Layout constants ──
#define CODE_BOX_W   52
#define CODE_BOX_H   64
#define CODE_BOX_R   12
#define CODE_GAP      8
#define CODE_COUNT    6
#define CODE_TOTAL_W  (CODE_COUNT * CODE_BOX_W + (CODE_COUNT - 1) * CODE_GAP)
#define CODE_START_X  ((SCREEN_W - CODE_TOTAL_W) / 2)
#define CODE_Y       200
#define SKIP_BTN_W   160
#define SKIP_BTN_H    40
#define SKIP_BTN_X   ((SCREEN_W - SKIP_BTN_W) / 2)
#define SKIP_BTN_Y   400

// ── State ──
static unsigned long screenEnteredMs = 0;
static unsigned long lastDotMs = 0;
static bool dotVisible = true;
static bool pairedTransition = false;
static unsigned long pairedMs = 0;

void pairingScreenDraw(const char* code) {
    if (code == nullptr) {
        code = supabaseGetPairingCode();
    }

    if (screenEnteredMs == 0) {
        screenEnteredMs = millis();
    }

    tft.fillScreen(Colors::BG_BASE);

    // Lemon imagotipo
    drawLemonImagotipo122(tft, (SCREEN_W - 122) / 2, 50);

    // Subtitle
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::top_center);
    tft.drawString("Vincula con la Lemon app", SCREEN_W / 2, 130, &SatoshiMedium18);

    if (pairedTransition) {
        // Paired state: show checkmark + greeting
        tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_BASE);
        tft.setTextDatum(lgfx::middle_center);
        tft.drawString("\\/", SCREEN_W / 2, CODE_Y + CODE_BOX_H / 2, &SatoshiBold40);

        char greetBuf[48];
        const char* tag = supabaseGetLemonTag();
        if (tag && tag[0]) {
            snprintf(greetBuf, sizeof(greetBuf), "Conectado con @%s", tag);
        } else {
            snprintf(greetBuf, sizeof(greetBuf), "Conectado!");
        }
        tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_BASE);
        tft.setTextDatum(lgfx::top_center);
        tft.drawString(greetBuf, SCREEN_W / 2, CODE_Y + CODE_BOX_H + 20, &Satoshi12);
        return;
    }

    // ── 6-character code boxes ──
    for (int i = 0; i < CODE_COUNT; i++) {
        int bx = CODE_START_X + i * (CODE_BOX_W + CODE_GAP);
        // Box background
        tft.fillSmoothRoundRect(bx, CODE_Y, CODE_BOX_W, CODE_BOX_H, CODE_BOX_R, Colors::BG_CARD);
        tft.drawRoundRect(bx, CODE_Y, CODE_BOX_W, CODE_BOX_H, CODE_BOX_R, Colors::CARD_BORDER);

        // Character
        char ch[2] = { code[i], '\0' };
        tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
        tft.setTextDatum(lgfx::middle_center);
        tft.drawString(ch, bx + CODE_BOX_W / 2, CODE_Y + CODE_BOX_H / 2, &SatoshiBold40);
    }

    // Instruction
    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::top_center);
    tft.drawString("Ingresa este codigo en la Lemon app", SCREEN_W / 2, CODE_Y + CODE_BOX_H + 16, &Satoshi9);

    // Pulsing dot (green when waiting)
    if (dotVisible) {
        tft.fillSmoothCircle(SCREEN_W / 2, CODE_Y + CODE_BOX_H + 44, 4, Colors::LEMON_GREEN);
    }

    // Skip button
    tft.fillSmoothRoundRect(SKIP_BTN_X, SKIP_BTN_Y, SKIP_BTN_W, SKIP_BTN_H, 12, Colors::BG_SURFACE);
    tft.drawRoundRect(SKIP_BTN_X, SKIP_BTN_Y, SKIP_BTN_W, SKIP_BTN_H, 12, Colors::DIVIDER);
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_SURFACE);
    tft.setTextDatum(lgfx::middle_center);
    tft.drawString("Saltar", SKIP_BTN_X + SKIP_BTN_W / 2, SKIP_BTN_Y + SKIP_BTN_H / 2, &Satoshi12);

    // Version badge
    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::top_center);
#if PRESS_EDITION
    tft.drawString("v" APP_VERSION " Press Edition", SCREEN_W / 2, SCREEN_H - 24, &Satoshi9);
#else
    tft.drawString("v" APP_VERSION, SCREEN_W / 2, SCREEN_H - 24, &Satoshi9);
#endif
}

void pairingScreenHandleTouch(const TouchEvent& evt) {
    if (evt.gesture != TOUCH_TAP) return;

    // Skip button
    if (touchInRect(evt.x, evt.y, SKIP_BTN_X, SKIP_BTN_Y, SKIP_BTN_W, SKIP_BTN_H)) {
        Serial.println("[Pairing] Skip → dashboard");
        screenEnteredMs = 0;
        pairedTransition = false;
        appSetScreen(SCREEN_DASHBOARD);
        return;
    }

    // Tap anywhere else: no-op (let the screen run)
}

void pairingScreenTick() {
    unsigned long now = millis();

    // Check if pairing happened
    if (!pairedTransition && supabaseGetPairingState() == PAIRING_PAIRED) {
        pairedTransition = true;
        pairedMs = now;
        pairingScreenDraw(nullptr);
        return;
    }

    // Paired transition: auto-switch to dashboard after 2s
    if (pairedTransition && (now - pairedMs >= 2000)) {
        screenEnteredMs = 0;
        pairedTransition = false;
        appSetScreen(SCREEN_DASHBOARD);
        return;
    }

    // Auto-skip to dashboard after timeout (press edition: show code briefly then move on)
    if (!pairedTransition && screenEnteredMs > 0 && (now - screenEnteredMs >= SUPA_PAIRING_SHOW_MS)) {
        Serial.println("[Pairing] Auto-skip → dashboard");
        screenEnteredMs = 0;
        appSetScreen(SCREEN_DASHBOARD);
        return;
    }

    // Pulsing dot animation (toggle every 500ms)
    if (!pairedTransition && (now - lastDotMs >= 500)) {
        lastDotMs = now;
        dotVisible = !dotVisible;
        // Redraw just the dot area
        int dotX = SCREEN_W / 2;
        int dotY = CODE_Y + CODE_BOX_H + 44;
        tft.fillCircle(dotX, dotY, 6, Colors::BG_BASE);  // Clear
        if (dotVisible) {
            tft.fillSmoothCircle(dotX, dotY, 4, Colors::LEMON_GREEN);
        }
    }

    // Run supabase loop to check for pairing events
    supabaseLoop();
}

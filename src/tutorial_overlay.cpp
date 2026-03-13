#include "tutorial_overlay.h"
#include "nvs_storage.h"
#include "display_manager.h"
#include "ui_dashboard.h"
#include "colors.h"
#include "config.h"
#include "data/satoshi_fonts.h"

// ── State ──
static bool    tutActive    = false;
static uint8_t tutStep      = 0;
static bool    tutDirty     = false;
static uint8_t tutStartStep = 0;    // First step in active range (inclusive)
static uint8_t tutEndStep   = 255;  // Last step in active range (inclusive, 255 = all)
static bool    proPending   = false; // Deferred pro tutorial (starts on dashboard entry)
static bool    proWarmup    = false; // One-frame warmup: let dashboard redraw before tutorial
static bool    resetFlag    = false; // Consumed by tutorialNeedsReset()

// ── Step definitions ──
struct TutSpotlight {
    int16_t x, y, w, h;
};

enum TooltipSide : uint8_t { TIP_CENTER, TIP_BELOW, TIP_ABOVE };

enum TutGesture : uint8_t {
    TUT_TAP,             // Tap anywhere advances
    TUT_SWIPE_V,         // Swipe up or down on spotlight zone
    TUT_TAP_ZONE,        // Tap inside spotlight area
    TUT_LONG_PRESS_Z0,   // Long press on header zone
    TUT_LONG_PRESS_ZONE, // Long press inside spotlight area
};

struct TutStep {
    const char*  title;
    const char*  body;
    TutSpotlight spot;       // {0,0,0,0} = no spotlight (welcome card)
    TooltipSide  side;
    TutGesture   gesture;    // Required gesture to advance
    bool         proOnly;    // Skip this step if not Pro mode
};

// Pro-only steps: indices 4, 5, 6
static const int PRO_START = 4;
static const int PRO_END   = 6;

static const int TUT_STEP_COUNT = 9;

// Spotlight rects — step 7 uses runtime z2Y/z2H, resolved in getStep()
static const TutStep STEPS_TEMPLATE[TUT_STEP_COUNT] = {
    // 0: Welcome — no spotlight, centered card
    { "Bienvenido",
      "Un tour rapido para\nconocer tu dashboard.",
      {0, 0, 0, 0}, TIP_CENTER, TUT_TAP, false },

    // 1: BTC Price zone
    { "Precio en vivo",
      "Bitcoin en tiempo real.\nSe actualiza solo.",
      {16, 48, 448, 80}, TIP_BELOW, TUT_TAP, false },

    // 2: Period carousel (CAROUSEL_X=360, CAROUSEL_W=90, screen coords)
    { "Temporalidad",
      "Desliza arriba o abajo\npara cambiar el periodo.",
      {358, 50, 94, 114}, TIP_BELOW, TUT_SWIPE_V, false },

    // 3: Chart area (Y=148 fixed, H resolved at runtime from z1H)
    { "Grafico",
      "Toca el grafico para cambiar\nentre linea y velas.",
      {16, 148, 448, 0}, TIP_BELOW, TUT_TAP_ZONE, false },

    // ── Pro-only steps (4-6) ──

    // 4: More timeframes (pro, same carousel rect as step 2)
    { "Mas periodos",
      "Ahora tenes 7 periodos.\nDesliza para explorarlos.",
      {358, 50, 94, 114}, TIP_BELOW, TUT_SWIPE_V, true },

    // 5: Pair selector (pro) — pair label at (30, 58) in screen coords
    { "Selector de par",
      "Toca aca para elegir entre\nUSD, ETH, SOL, ARS y oro.",
      {30, 56, 116, 30}, TIP_BELOW, TUT_TAP_ZONE, true },

    // 6: Polymarket mini-game (pro) — z2 resolved at runtime
    { "Predicciones",
      "Manten presionado en el\ndolar para jugar predicciones.",
      {16, 0, 448, 0}, TIP_ABOVE, TUT_TAP, true },

    // ── End pro-only ──

    // 7: Dollar section (z2 — resolved at runtime)
    { "Dolar digital",
      "Cotizacion USDT/ARS en vivo.\nToca para cambiar el estilo.",
      {16, 0, 448, 0}, TIP_ABOVE, TUT_TAP, false },

    // 8: Settings header
    { "Ajustes",
      "Manten presionado el header\npara abrir la configuracion.",
      {16, 0, 448, 44}, TIP_BELOW, TUT_LONG_PRESS_Z0, false },
};

// Get step with runtime zone values resolved
static TutStep getStep(uint8_t idx) {
    TutStep s = STEPS_TEMPLATE[idx];
    if (idx == 3) {
        // Chart area: Y=148 (fixed), H = Z1 bottom - 148
        s.spot.h = (int16_t)(48 + dashboardGetZ1H() - 148);  // Z1_Y=48
    }
    if (idx == 6 || idx == 7) {
        s.spot.y = (int16_t)dashboardGetZ2Y();
        s.spot.h = (int16_t)dashboardGetZ2H();
    }
    return s;
}

// Effective end index (inclusive)
static uint8_t effEnd() {
    return (tutEndStep < TUT_STEP_COUNT) ? tutEndStep : (TUT_STEP_COUNT - 1);
}

// Count visible steps in active range
static int countVisibleSteps() {
    int n = 0;
    bool pro = nvsGetProMode();
    for (int i = tutStartStep; i <= effEnd(); i++) {
        if (!STEPS_TEMPLATE[i].proOnly || pro) n++;
    }
    return n;
}

// Get visible index (1-based) for current step within active range
static int visibleIndex(uint8_t step) {
    bool pro = nvsGetProMode();
    int idx = 0;
    for (int i = tutStartStep; i <= step && i <= effEnd(); i++) {
        if (!STEPS_TEMPLATE[i].proOnly || pro) idx++;
    }
    return idx;
}

// Is this the first visible step in active range?
static bool isFirstVisible() {
    bool pro = nvsGetProMode();
    for (int i = tutStartStep; i < tutStep; i++) {
        if (!STEPS_TEMPLATE[i].proOnly || pro) return false;
    }
    return true;
}

// Is this the last visible step in active range?
static bool isLastVisible() {
    bool pro = nvsGetProMode();
    for (int i = tutStep + 1; i <= effEnd(); i++) {
        if (!STEPS_TEMPLATE[i].proOnly || pro) return false;
    }
    return true;
}

// ── Advance step (skips proOnly when not Pro) ──
static void advanceStep() {
    tutStep++;
    while (tutStep <= effEnd() && STEPS_TEMPLATE[tutStep].proOnly && !nvsGetProMode()) {
        tutStep++;
    }
    if (tutStep > effEnd()) {
        tutActive = false;
        if (tutStartStep == 0) nvsSetTutorialDone(true);
        if (tutStartStep == PRO_START) nvsSetProTutDone(true);
        Serial.println("[Tutorial] Completed");
    } else {
        tutDirty = true;
        Serial.printf("[Tutorial] Step %d/%d\n", visibleIndex(tutStep), countVisibleSteps());
    }
}

// ── Go back one visible step ──
static void goBack() {
    if (tutStep <= tutStartStep) return;
    bool pro = nvsGetProMode();
    int prev = tutStep - 1;
    while (prev > (int)tutStartStep && STEPS_TEMPLATE[prev].proOnly && !pro) {
        prev--;
    }
    if (prev < (int)tutStartStep) return;
    if (STEPS_TEMPLATE[prev].proOnly && !pro) return;
    tutStep = prev;
    tutDirty = true;
    Serial.printf("[Tutorial] Back to step %d/%d\n", visibleIndex(tutStep), countVisibleSteps());
}

// ── Dimming: 4 black rects around spotlight ──
static void drawDimming(const TutSpotlight& sp) {
    if (sp.w == 0 && sp.h == 0) {
        tft.fillRect(0, 0, SCREEN_W, SCREEN_H, Colors::BG_BASE);
        return;
    }
    if (sp.y > 0)
        tft.fillRect(0, 0, SCREEN_W, sp.y, Colors::BG_BASE);
    int bot = sp.y + sp.h;
    if (bot < SCREEN_H)
        tft.fillRect(0, bot, SCREEN_W, SCREEN_H - bot, Colors::BG_BASE);
    if (sp.x > 0)
        tft.fillRect(0, sp.y, sp.x, sp.h, Colors::BG_BASE);
    int right = sp.x + sp.w;
    if (right < SCREEN_W)
        tft.fillRect(right, sp.y, SCREEN_W - right, sp.h, Colors::BG_BASE);
}

// ── Spotlight border (static LEMON_GREEN) ──
static void drawSpotlightBorder(const TutSpotlight& sp) {
    if (sp.w == 0 && sp.h == 0) return;
    tft.drawRoundRect(sp.x - 2, sp.y - 2, sp.w + 4, sp.h + 4, 8, Colors::LEMON_GREEN);
    tft.drawRoundRect(sp.x - 1, sp.y - 1, sp.w + 2, sp.h + 2, 7, Colors::LEMON_GREEN);
}

// ── Multiline text drawing helper ──
static void drawMultilineText(int16_t x, int16_t y, const char* text,
                              const lgfx::IFont* font, uint16_t color, int lineH) {
    tft.setTextColor(color, Colors::BG_CARD);
    tft.setTextDatum(lgfx::top_left);
    const char* p = text;
    int cy = y;
    while (*p) {
        const char* nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        char line[64];
        if (len >= (int)sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        tft.drawString(line, x, cy, font);
        cy += lineH;
        if (!nl) break;
        p = nl + 1;
    }
}

// Count lines in a string
static int countLines(const char* text) {
    int n = 1;
    for (const char* p = text; *p; p++) {
        if (*p == '\n') n++;
    }
    return n;
}

// ── Tooltip layout constants ──
static const int TOOLTIP_W      = 340;
static const int TT_PAD         = 20;
static const int TT_TITLE_H     = 32;
static const int TT_BODY_LINE_H = 26;
static const int TT_FOOTER_H    = 22;

// Compute tooltip height and Y position
static void tooltipGeometry(const TutStep& step, int& outH, int& outTY) {
    int bodyLines  = countLines(step.body);
    int titleLines = countLines(step.title);
    outH = TT_PAD + (titleLines * TT_TITLE_H) + 8 + (bodyLines * TT_BODY_LINE_H) + 10 + TT_FOOTER_H + TT_PAD;

    if (step.side == TIP_CENTER) {
        outTY = (SCREEN_H - outH) / 2;
    } else if (step.side == TIP_BELOW) {
        outTY = step.spot.y + step.spot.h + 10;
        if (outTY + outH > SCREEN_H - 10) outTY = SCREEN_H - 10 - outH;
    } else { // TIP_ABOVE
        outTY = step.spot.y - outH - 10;
        if (outTY < 10) outTY = 10;
    }
}

// ── Tooltip card ──
static void drawTooltip(const TutStep& step) {
    int TOOLTIP_H, ty;
    tooltipGeometry(step, TOOLTIP_H, ty);

    int tx = (SCREEN_W - TOOLTIP_W) / 2;

    // Card background with border
    tft.fillSmoothRoundRect(tx, ty, TOOLTIP_W, TOOLTIP_H, 12, Colors::CARD_BORDER);
    tft.fillSmoothRoundRect(tx + 1, ty + 1, TOOLTIP_W - 2, TOOLTIP_H - 2, 11, Colors::BG_CARD);

    // Top highlight line
    tft.drawFastHLine(tx + 12, ty + 1, TOOLTIP_W - 24, Colors::CARD_BORDER);

    // Title in LEMON_GREEN (left)
    int cy = ty + TT_PAD;
    drawMultilineText(tx + TT_PAD, cy, step.title,
                      &SatoshiBold24, Colors::LEMON_GREEN, TT_TITLE_H);

    // "Saltar" at top-right (dismiss-style)
    tft.setTextDatum(lgfx::top_right);
    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
    tft.drawString("Saltar", tx + TOOLTIP_W - TT_PAD, cy + 4, &Satoshi12);

    cy += countLines(step.title) * TT_TITLE_H + 8;

    // Body
    drawMultilineText(tx + TT_PAD, cy, step.body,
                      &SatoshiMedium18, Colors::TEXT_SECONDARY, TT_BODY_LINE_H);
    cy += countLines(step.body) * TT_BODY_LINE_H + 12;

    // Footer: "< Atras" left | "N/M" center | "Siguiente >" right
    if (!isFirstVisible()) {
        tft.setTextDatum(lgfx::top_left);
        tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        tft.drawString("< Atras", tx + TT_PAD, cy, &Satoshi12);
    }

    char counter[8];
    snprintf(counter, sizeof(counter), "%d/%d", visibleIndex(tutStep), countVisibleSteps());
    tft.setTextDatum(lgfx::top_center);
    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
    tft.drawString(counter, tx + TOOLTIP_W / 2, cy, &Satoshi12);

    tft.setTextDatum(lgfx::top_right);
    if (isLastVisible()) {
        tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
        tft.drawString("Listo", tx + TOOLTIP_W - TT_PAD, cy, &Satoshi12);
    } else {
        tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
        tft.drawString("Siguiente >", tx + TOOLTIP_W - TT_PAD, cy, &Satoshi12);
    }
}

// ── Hit tests ──

// "Saltar" — top-right of tooltip
static bool hitTestSkip(int16_t tapX, int16_t tapY) {
    TutStep step = getStep(tutStep);
    int TOOLTIP_H, ty;
    tooltipGeometry(step, TOOLTIP_H, ty);
    int tx = (SCREEN_W - TOOLTIP_W) / 2;

    int btnRight = tx + TOOLTIP_W - TT_PAD + 8;
    int btnLeft  = btnRight - 90;
    int btnTop   = ty + TT_PAD - 4;
    int btnBot   = btnTop + 32;
    return (tapX >= btnLeft && tapX <= btnRight && tapY >= btnTop && tapY <= btnBot);
}

// "< Atras" — footer left
static bool hitTestBack(int16_t tapX, int16_t tapY) {
    if (isFirstVisible()) return false;
    TutStep step = getStep(tutStep);
    int TOOLTIP_H, ty;
    tooltipGeometry(step, TOOLTIP_H, ty);
    int tx = (SCREEN_W - TOOLTIP_W) / 2;

    int footerY = ty + TOOLTIP_H - TT_PAD - TT_FOOTER_H;
    int btnLeft  = tx + TT_PAD - 8;
    int btnRight = btnLeft + 100;
    return (tapX >= btnLeft && tapX <= btnRight && tapY >= footerY - 6 && tapY <= footerY + 30);
}

// "Siguiente >" / "Listo" — footer right
static bool hitTestNext(int16_t tapX, int16_t tapY) {
    TutStep step = getStep(tutStep);
    int TOOLTIP_H, ty;
    tooltipGeometry(step, TOOLTIP_H, ty);
    int tx = (SCREEN_W - TOOLTIP_W) / 2;

    int footerY  = ty + TOOLTIP_H - TT_PAD - TT_FOOTER_H;
    int btnRight = tx + TOOLTIP_W - TT_PAD + 8;
    int btnLeft  = btnRight - 120;
    return (tapX >= btnLeft && tapX <= btnRight && tapY >= footerY - 6 && tapY <= footerY + 30);
}

// ── Check if gesture matches current step requirement ──
static bool gestureMatches(const TutStep& step, const TouchEvent& evt) {
    switch (step.gesture) {
        case TUT_TAP:
            return evt.gesture == TOUCH_TAP;

        case TUT_SWIPE_V:
            return evt.gesture == TOUCH_SWIPE_UP  || evt.gesture == TOUCH_SWIPE_DOWN ||
                   evt.gesture == TOUCH_FLING_UP   || evt.gesture == TOUCH_FLING_DOWN;

        case TUT_TAP_ZONE:
            if (evt.gesture != TOUCH_TAP) return false;
            return (evt.x >= step.spot.x && evt.x < step.spot.x + step.spot.w &&
                    evt.y >= step.spot.y && evt.y < step.spot.y + step.spot.h);

        case TUT_LONG_PRESS_Z0:
            return evt.gesture == TOUCH_LONG_PRESS && evt.y < 44;

        case TUT_LONG_PRESS_ZONE:
            if (evt.gesture != TOUCH_LONG_PRESS) return false;
            return (evt.x >= step.spot.x && evt.x < step.spot.x + step.spot.w &&
                    evt.y >= step.spot.y && evt.y < step.spot.y + step.spot.h);

        default:
            return false;
    }
}

// ── Public API ──

void tutorialInit() {
    if (!nvsGetTutorialDone()) {
        tutActive = true;
        resetFlag = true;
        tutStartStep = 0;
        tutEndStep = TUT_STEP_COUNT - 1;
        tutStep = 0;
        while (tutStep <= effEnd() && STEPS_TEMPLATE[tutStep].proOnly && !nvsGetProMode()) {
            tutStep++;
        }
        tutDirty = true;
        Serial.println("[Tutorial] Started — first boot");
    }
}

void tutorialStart() {
    tutActive = true;
    resetFlag = true;
    tutStartStep = 0;
    tutEndStep = TUT_STEP_COUNT - 1;
    tutStep = 0;
    while (tutStep <= effEnd() && STEPS_TEMPLATE[tutStep].proOnly && !nvsGetProMode()) {
        tutStep++;
    }
    tutDirty = true;
    Serial.println("[Tutorial] Restarted from settings");
}

void tutorialStartPro() {
    // Deferred — will actually start when tutorialCheckPending() is called on dashboard
    proPending = true;
    Serial.println("[Tutorial] Pro tutorial queued");
}

void tutorialCheckPending() {
    if (!proPending && !proWarmup) return;

    if (proPending) {
        // Frame 1: mark dashboard dirty and wait one frame for sprites to redraw
        proPending = false;
        proWarmup = true;
        resetFlag = true;
        dashboardMarkAllDirty();
        Serial.println("[Tutorial] Pro warmup — dashboard redrawing");
        return;
    }

    // Frame 2: sprites are fresh, now activate tutorial
    proWarmup = false;
    tutActive = true;
    tutStartStep = PRO_START;
    tutEndStep = PRO_END;
    tutStep = PRO_START;
    tutDirty = true;
    Serial.println("[Tutorial] Pro features mini-tutorial started");
}

bool tutorialIsActive() {
    return tutActive;
}

bool tutorialNeedsReset() {
    if (!resetFlag) return false;
    resetFlag = false;
    return true;
}

bool tutorialNeedsDraw() {
    if (!tutActive || !tutDirty) return false;
    tutDirty = false;
    return true;
}

// Logo area constants (isotipo 28x28 at MARGIN=16, centered vertically in 44px header)
static const int LOGO_X = 16;
static const int LOGO_Y = 0;
static const int LOGO_W = 140;  // Imagotipo (icon + wordmark)
static const int LOGO_H = 44;   // Full header height

void tutorialDraw() {
    if (!tutActive) return;

    TutStep step = getStep(tutStep);

    // 1. Dim around spotlight
    drawDimming(step.spot);

    // 2. Always show logo isotipo in header (push from sprZ0)
    //    Skip if spotlight already covers the header area
    bool spotCoversLogo = (step.spot.w > 0 && step.spot.h > 0 &&
                           step.spot.y == 0 && step.spot.h >= LOGO_H);
    if (!spotCoversLogo) {
        dashboardPushSpotlight(LOGO_X, LOGO_Y, LOGO_W, LOGO_H);
    }

    // 3. Push dashboard sprites clipped to spotlight rect
    if (step.spot.w > 0 && step.spot.h > 0) {
        dashboardPushSpotlight(step.spot.x, step.spot.y, step.spot.w, step.spot.h);
    }

    // 4. Spotlight border
    drawSpotlightBorder(step.spot);

    // 5. Tooltip card
    drawTooltip(step);
}

void tutorialHandleTouch(const TouchEvent& evt) {
    if (!tutActive) return;

    if (evt.gesture == TOUCH_TAP) {
        // "Saltar" — dismiss tutorial
        if (hitTestSkip(evt.x, evt.y)) {
            tutActive = false;
            if (tutStartStep == 0) nvsSetTutorialDone(true);
            if (tutStartStep == PRO_START) nvsSetProTutDone(true);
            Serial.println("[Tutorial] Skipped");
            return;
        }

        // "Siguiente >" / "Listo" — advance
        if (hitTestNext(evt.x, evt.y)) {
            advanceStep();
            return;
        }

        // "< Atras" — go back
        if (hitTestBack(evt.x, evt.y)) {
            goBack();
            return;
        }
    }

    // Gesture-based advancement (alternative to Siguiente button)
    TutStep step = getStep(tutStep);
    if (gestureMatches(step, evt)) {
        advanceStep();
    }
}

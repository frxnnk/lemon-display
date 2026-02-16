#include "ui_dashboard.h"
#include "display_manager.h"
#include "ui_components.h"
#include "colors.h"
#include "config.h"
#include "touch_utils.h"
#include "data/lemon_logo.h"
#include "data/satoshi_fonts.h"

// ══════════════════════════════════════════
//  LAYOUT v3.1b (480x480, BTC hero + Lemon dollar)
//  No dominance bar, no coin pills — BTC/USD only
// ══════════════════════════════════════════

#define MARGIN     16
#define GAP         6
#define CARD_PAD   14
#define CARD_W    448   // 480 - 2*MARGIN
#define CARD_R     16

// Zone definitions (Y, H) — 2 zones only
#define Z0_Y    0
#define Z0_H   44      // Header
#define Z1_Y   48
#define Z1_H  300      // BTC Hero + Chart (expanded)
#define Z2_Y  354
#define Z2_H  120      // Lemon Dollar (slightly taller)

// ── Sparkline period badge layout (inside Z1) ──
#define BADGE_W     90
#define BADGE_H     30
#define BADGE_GAP    6
#define BADGE_COUNT  3
#define BADGE_TOTAL_W (BADGE_W * BADGE_COUNT + BADGE_GAP * (BADGE_COUNT - 1))
#define BADGE_START_X (MARGIN + (CARD_W - BADGE_TOTAL_W) / 2)
#define BADGE_Y_IN_Z1 100  // Y within Z1 sprite (below price + glow)

// ── Non-blocking flash state ──
static bool     flashActive   = false;
static uint8_t  flashZoneId   = 255;
static uint32_t flashStartMs  = 0;
static const uint32_t FLASH_DURATION_MS = 150;

// ── Reusable sprite for zone rendering ──
static LGFX_Sprite zoneSprite(&tft);
static bool spriteCreated = false;

static void ensureSprite(int w, int h) {
    if (spriteCreated) zoneSprite.deleteSprite();
    zoneSprite.setPsram(true);
    zoneSprite.setColorDepth(16);
    zoneSprite.createSprite(w, h);
    zoneSprite.fillSprite(Colors::BG_BASE);
    spriteCreated = true;
}

static void pushZone(int y) {
    zoneSprite.pushSprite(0, y);
    zoneSprite.deleteSprite();
    spriteCreated = false;
}

// ── Helper: green glow circles behind price ──
static void drawPriceGlow(LGFX_Sprite& spr, int cx, int cy) {
    spr.fillSmoothCircle(cx, cy, 50, Colors::GLOW_1);
    spr.fillSmoothCircle(cx, cy, 38, Colors::GLOW_2);
    spr.fillSmoothCircle(cx, cy, 28, Colors::GLOW_3);
    spr.fillSmoothCircle(cx, cy, 18, Colors::GLOW_4);
}

// ── Helper: get zone Y/H ──
static bool getZoneBounds(uint8_t zoneId, int& y, int& h) {
    switch (zoneId) {
        case 0: y = Z0_Y; h = Z0_H; return true;
        case 1: y = Z1_Y; h = Z1_H; return true;
        case 2: y = Z2_Y; h = Z2_H; return true;
        default: return false;
    }
}

static const char* periodLabels[] = { "1h", "24h", "7d" };

// ══════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════

void dashboardSetup() {
    tft.fillScreen(Colors::BG_BASE);
}

// ══════════════════════════════════════════
//  Z0: HEADER (44px)
// ══════════════════════════════════════════

void dashboardDrawHeader(const char* timeStr, bool offline, bool liveMode) {
    ensureSprite(SCREEN_W, Z0_H);
    zoneSprite.fillSprite(Colors::BG_BASE);

    // Full imagotipo (122x28 — icon + LEMON wordmark)
    int logoX = MARGIN;
    int logoY = (Z0_H - 28) / 2;
    drawLemonImagotipo122(zoneSprite, logoX, logoY);

    if (offline) {
        int badgeW = 90, badgeH = 24;
        int badgeX = SCREEN_W - badgeW - MARGIN;
        int badgeY = (Z0_H - badgeH) / 2;
        zoneSprite.fillSmoothRoundRect(badgeX, badgeY, badgeW, badgeH, 8, Colors::NEGATIVE);
        zoneSprite.setTextColor(Colors::TEXT_PRIMARY, Colors::NEGATIVE);
        zoneSprite.setTextDatum(lgfx::middle_center);
        zoneSprite.drawString("OFFLINE", badgeX + badgeW / 2, badgeY + badgeH / 2, &Satoshi12);
    } else {
        // Live mode indicator (small dot + "LIVE" before time)
        int timeX = SCREEN_W - MARGIN;
        if (liveMode) {
            int liveX = SCREEN_W - MARGIN - 160;
            zoneSprite.fillSmoothCircle(liveX, Z0_H / 2, 4, Colors::NEGATIVE);
            zoneSprite.setTextColor(Colors::NEGATIVE, Colors::BG_BASE);
            zoneSprite.setTextDatum(lgfx::middle_left);
            zoneSprite.drawString("LIVE", liveX + 8, Z0_H / 2, &Satoshi9);
        }

        zoneSprite.setTextColor(Colors::LEMON_GREEN, Colors::BG_BASE);
        zoneSprite.setTextDatum(lgfx::middle_right);
        zoneSprite.drawString(timeStr, timeX, Z0_H / 2, &fonts::Orbitron_Light_24);
    }

    // Separator line
    zoneSprite.drawFastHLine(MARGIN, Z0_H - 1, CARD_W, Colors::DIVIDER);

    pushZone(Z0_Y);
}

// ══════════════════════════════════════════
//  Z1: BTC HERO + CHART (300px)
// ══════════════════════════════════════════

void dashboardDrawBtcHero(const BtcPrice& btc, const SparklineData& spark, uint8_t selectedPeriod) {
    ensureSprite(SCREEN_W, Z1_H);

    // Hero card (accent border, radius 20)
    drawHeroCard(zoneSprite, MARGIN, 0, CARD_W, Z1_H);

    if (!btc.valid) {
        drawCentered(zoneSprite, "Bitcoin...", Z1_H / 2 - 10,
                     &SatoshiMedium18, Colors::TEXT_SECONDARY);
        pushZone(Z1_Y);
        return;
    }

    // ── Top row: BTC dot + "Bitcoin" + 1h change badge ──
    drawCoinDot(zoneSprite, MARGIN + CARD_PAD + 8, 16, 6, COIN_BTC);
    zoneSprite.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
    zoneSprite.setTextDatum(lgfx::top_left);
    zoneSprite.drawString("Bitcoin", MARGIN + CARD_PAD + 20, 10, &Satoshi12);

    // 1h change badge (top right)
    {
        float var = btc.change1h;
        bool pos = var >= 0;
        uint16_t badgeBg = pos ? Colors::BADGE_BG_POS : Colors::BADGE_BG_NEG;
        uint16_t badgeTxt = pos ? Colors::POSITIVE : Colors::NEGATIVE;
        char varBuf[16];
        snprintf(varBuf, sizeof(varBuf), "%s%.1f%% 1h", pos ? "+" : "", var);

        int vBadgeW = 90;
        int vBadgeH = 20;
        int vBadgeX = CARD_W - CARD_PAD - vBadgeW;
        int vBadgeY = 10;
        zoneSprite.fillSmoothRoundRect(vBadgeX, vBadgeY, vBadgeW, vBadgeH, vBadgeH / 2, badgeBg);
        zoneSprite.setTextColor(badgeTxt, badgeBg);
        zoneSprite.setTextDatum(lgfx::middle_center);
        zoneSprite.drawString(varBuf, vBadgeX + vBadgeW / 2, vBadgeY + vBadgeH / 2, &Satoshi9);
    }

    // ── Main BTC price with glow ──
    int priceCenterY = 56;
    drawPriceGlow(zoneSprite, SCREEN_W / 2, priceCenterY);

    char priceBuf[20];
    formatBtcPrice(priceBuf, sizeof(priceBuf), btc.usd);

    zoneSprite.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
    zoneSprite.setTextDatum(lgfx::middle_center);
    zoneSprite.drawString(priceBuf, SCREEN_W / 2, priceCenterY, &SatoshiBold40);

    // ── Period badges row (1h / 24h / 7d) — centered ──
    {
        int badgeY = BADGE_Y_IN_Z1;
        float changes[] = { btc.change1h, btc.change24h, btc.change7d };
        for (int i = 0; i < 3; i++) {
            bool sel = (i == selectedPeriod);
            int bx = BADGE_START_X - MARGIN + i * (BADGE_W + BADGE_GAP);
            drawChangeBadge(zoneSprite, bx, badgeY, BADGE_W, BADGE_H,
                           changes[i], periodLabels[i], sel);
        }
    }

    // ── Sparkline chart (bottom portion of hero) ──
    if (spark.valid && spark.count >= 2) {
        int chartX = MARGIN + CARD_PAD;
        int chartY = BADGE_Y_IN_Z1 + BADGE_H + 10;
        int chartW = CARD_W - 2 * CARD_PAD;
        int chartH = Z1_H - chartY - 10;

        if (chartH > 10) {
            drawSparkline(zoneSprite, chartX, chartY, chartW, chartH,
                          spark, Colors::CHART_LINE, Colors::CHART_FILL);
        }
    }

    pushZone(Z1_Y);
}

// ══════════════════════════════════════════
//  Z2: LEMON DOLLAR (120px)
// ══════════════════════════════════════════

void dashboardDrawLemonDollar(const LemonPrice& lemon) {
    ensureSprite(SCREEN_W, Z2_H);

    drawGlassCard(zoneSprite, MARGIN, 0, CARD_W, Z2_H, CARD_R);

    if (!lemon.valid) {
        drawCentered(zoneSprite, "Dolar Lemon...", Z2_H / 2 - 6,
                     &Satoshi12, Colors::TEXT_SECONDARY);
        pushZone(Z2_Y);
        return;
    }

    // Title
    zoneSprite.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
    zoneSprite.setTextDatum(lgfx::top_left);
    zoneSprite.drawString("Dolar Lemon (USDT/ARS)", MARGIN + CARD_PAD, 12, &Satoshi9);

    // Two columns: Compra (bid) | Venta (ask)
    int colW = (CARD_W - 2 * CARD_PAD) / 2;
    int leftX = MARGIN + CARD_PAD;
    int rightX = MARGIN + CARD_PAD + colW;

    // Labels
    int labelY = 34;
    zoneSprite.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
    zoneSprite.setTextDatum(lgfx::top_center);
    zoneSprite.drawString("Compra", leftX + colW / 2, labelY, &Satoshi9);
    zoneSprite.drawString("Venta", rightX + colW / 2, labelY, &Satoshi9);

    // Prices (large)
    char bidBuf[20], askBuf[20];
    formatArsPrice(bidBuf, sizeof(bidBuf), lemon.bid);
    formatArsPrice(askBuf, sizeof(askBuf), lemon.ask);

    int priceY = 54;
    zoneSprite.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
    zoneSprite.setTextDatum(lgfx::top_center);
    zoneSprite.drawString(bidBuf, leftX + colW / 2, priceY, &SatoshiBold24);
    zoneSprite.drawString(askBuf, rightX + colW / 2, priceY, &SatoshiBold24);

    // Spread percentage
    if (lemon.ask > 0 && lemon.bid > 0) {
        float spread = ((lemon.ask - lemon.bid) / lemon.bid) * 100.0f;
        char spreadBuf[24];
        snprintf(spreadBuf, sizeof(spreadBuf), "spread %.2f%%", spread);
        zoneSprite.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        zoneSprite.setTextDatum(lgfx::top_center);
        zoneSprite.drawString(spreadBuf, SCREEN_W / 2, 92, &Satoshi9);
    }

    pushZone(Z2_Y);
}

// ══════════════════════════════════════════
//  FULL REDRAW
// ══════════════════════════════════════════

void dashboardDrawAll(const char* timeStr,
                      const BtcPrice& btc, const SparklineData& spark,
                      uint8_t selectedPeriod,
                      const LemonPrice& lemon,
                      bool offline, bool liveMode) {
    tft.fillScreen(Colors::BG_BASE);
    dashboardDrawHeader(timeStr, offline, liveMode);
    dashboardDrawBtcHero(btc, spark, selectedPeriod);
    dashboardDrawLemonDollar(lemon);
}

// ══════════════════════════════════════════
//  LOADING SCREEN — Logo + progress bar
// ══════════════════════════════════════════

#define LOAD_CX         (SCREEN_W / 2)
#define LOAD_LOGO_W     244
#define LOAD_LOGO_H     56
#define LOAD_LOGO_X     ((SCREEN_W - LOAD_LOGO_W) / 2)
#define LOAD_LOGO_Y     170
#define LOAD_BAR_W      300
#define LOAD_BAR_H      6
#define LOAD_BAR_R      (LOAD_BAR_H / 2)
#define LOAD_BAR_X      ((SCREEN_W - LOAD_BAR_W) / 2)
#define LOAD_BAR_Y      270
#define LOAD_STATUS_Y   296

static const char* loadStatusTexts[] = {
    "Iniciando...",
    "Conectando WiFi...",
    "Sincronizando hora...",
    "Obteniendo datos...",
    "Obteniendo dolar...",
    "Cargando grafico...",
    "Listo!"
};

void dashboardDrawLoading(LoadPhase phase) {
    if (phase == LOAD_LOGO) {
        tft.fillScreen(Colors::BG_BASE);

        // Full imagotipo (244x56 — icon + LEMON wordmark, centered)
        drawLemonImagotipo244(tft, LOAD_LOGO_X, LOAD_LOGO_Y);

        // Progress bar background
        tft.fillSmoothRoundRect(LOAD_BAR_X, LOAD_BAR_Y, LOAD_BAR_W, LOAD_BAR_H, LOAD_BAR_R, Colors::BG_ELEVATED);

        // Version at bottom
        {
            LGFX_Sprite verSpr(&tft);
            verSpr.setColorDepth(16);
            verSpr.createSprite(SCREEN_W, 20);
            verSpr.fillSprite(Colors::BG_BASE);
            verSpr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
            verSpr.setTextDatum(lgfx::top_center);
            verSpr.drawString("v" APP_VERSION, SCREEN_W / 2, 0, &Satoshi9);
            verSpr.pushSprite(0, SCREEN_H - 30);
            verSpr.deleteSprite();
        }
    }

    // Progress bar fill (grows with each phase)
    int fillW = (int)((long)LOAD_BAR_W * (int)phase / (int)LOAD_DONE);
    if (fillW > 0) {
        if (fillW < LOAD_BAR_H) fillW = LOAD_BAR_H;  // min width for rounding
        tft.fillSmoothRoundRect(LOAD_BAR_X, LOAD_BAR_Y, fillW, LOAD_BAR_H, LOAD_BAR_R, Colors::LEMON_GREEN);
    }

    // Status text (sprite to cleanly replace previous text)
    {
        LGFX_Sprite stSpr(&tft);
        stSpr.setColorDepth(16);
        stSpr.createSprite(SCREEN_W, 24);
        stSpr.fillSprite(Colors::BG_BASE);

        uint16_t textColor = (phase == LOAD_DONE) ? Colors::LEMON_GREEN : Colors::TEXT_SECONDARY;
        stSpr.setTextColor(textColor, Colors::BG_BASE);
        stSpr.setTextDatum(lgfx::top_center);
        stSpr.drawString(loadStatusTexts[(int)phase], SCREEN_W / 2, 0, &Satoshi12);
        stSpr.pushSprite(0, LOAD_STATUS_Y);
        stSpr.deleteSprite();
    }
}

// ══════════════════════════════════════════
//  OFFLINE INDICATOR
// ══════════════════════════════════════════

void dashboardDrawOffline() {
    dashboardDrawHeader("--:--:--", true);
}

// ══════════════════════════════════════════
//  NON-BLOCKING TOUCH FLASH
// ══════════════════════════════════════════

void dashboardStartFlash(uint8_t zoneId) {
    int y, h;
    if (!getZoneBounds(zoneId, y, h)) return;

    flashActive   = true;
    flashZoneId   = zoneId;
    flashStartMs  = millis();

    tft.drawRoundRect(MARGIN - 1, y - 1, CARD_W + 2, h + 2, CARD_R, Colors::LEMON_GREEN);
}

void dashboardUpdateFlash() {
    if (!flashActive) return;

    if (millis() - flashStartMs >= FLASH_DURATION_MS) {
        int y, h;
        if (getZoneBounds(flashZoneId, y, h)) {
            tft.drawRoundRect(MARGIN - 1, y - 1, CARD_W + 2, h + 2, CARD_R, Colors::BG_BASE);
        }
        flashActive  = false;
        flashZoneId  = 255;
    }
}

// ══════════════════════════════════════════
//  ZONE HIT-TEST
// ══════════════════════════════════════════

static const int DASH_ZONE_BOUNDS[][2] = {
    {  Z0_Y, Z0_Y + Z0_H },  // Zone 0: Header
    {  Z1_Y, Z1_Y + Z1_H },  // Zone 1: BTC Hero
    {  Z2_Y, Z2_Y + Z2_H },  // Zone 2: Lemon Dollar
};
static const int DASH_NUM_ZONES = 3;

static uint8_t dashHitTestZone(int16_t y) {
    for (int i = 0; i < DASH_NUM_ZONES; i++) {
        if (y >= DASH_ZONE_BOUNDS[i][0] && y < DASH_ZONE_BOUNDS[i][1]) {
            return i;
        }
    }
    return 255;
}

// ── Sub-zone hit tests ──

uint8_t dashboardHitTestPeriodBadge(int16_t x, int16_t y) {
    int badgeScreenY = Z1_Y + BADGE_Y_IN_Z1;
    if (y < badgeScreenY || y >= badgeScreenY + BADGE_H) return 255;

    for (int i = 0; i < BADGE_COUNT; i++) {
        int bx = BADGE_START_X + i * (BADGE_W + BADGE_GAP);
        if (x >= bx && x < bx + BADGE_W) {
            return (uint8_t)i;
        }
    }
    return 255;
}

static DashboardTouchCB dashTouchCB = nullptr;

void dashboardSetTouchCallback(DashboardTouchCB cb) {
    dashTouchCB = cb;
}

void dashboardHandleTouch(const TouchEvent& evt) {
    if (evt.gesture == TOUCH_NONE) return;

    uint8_t zoneId = dashHitTestZone(evt.y);
    if (zoneId == 255) return;

    if (dashTouchCB) {
        dashTouchCB(evt, zoneId);
    }
}

#include "ui_dashboard.h"
#include "display_manager.h"
#include "ui_components.h"
#include "colors.h"
#include "config.h"
#include "touch_utils.h"
#include "supabase_client.h"
#include "data/lemon_logo.h"
#include "data/satoshi_fonts.h"
#include <cmath>

// ══════════════════════════════════════════
//  LAYOUT v4.0 (480x480, BTC hero + Lemon dollar)
// ══════════════════════════════════════════

#define MARGIN     16
#define GAP         6
#define CARD_PAD   14
#define CARD_W    448   // 480 - 2*MARGIN
#define CARD_R     16

// Zone definitions (Y, H) — Z0 is fixed, Z1/Z2 are runtime for layout presets
#define Z0_Y    0
#define Z0_H   44      // Header (fixed)
#define Z1_Y   48

// Runtime zone heights (set by dashboardSetLayout)
static int z1H = 300;  // Default: standard layout
static int z2H = 120;
static int z2Y = 354;  // Z1_Y + z1H + GAP
static uint8_t currentLayout = 0;

// ── Pair label + dropdown layout (inside Z1) ──
#define PAIR_LABEL_X     (MARGIN + CARD_PAD)  // 30
#define PAIR_LABEL_Y     6
#define PAIR_LABEL_W     140  // Touch hit area width
#define PAIR_LABEL_H     28   // Touch hit area height

#define DROPDOWN_X       (MARGIN + CARD_PAD)  // 30
#define DROPDOWN_Y       28                    // Below label
#define DROPDOWN_W       130
#define DROPDOWN_ITEM_H  26
#define DROPDOWN_PAD     4
#define DROPDOWN_R       10

// ── Period carousel layout (inside Z1, right of price) ──
#define CAROUSEL_W        90
#define CAROUSEL_ITEM_H   34
#define CAROUSEL_ITEM_GAP  2
#define CAROUSEL_X        (MARGIN + CARD_W - CARD_PAD - CAROUSEL_W)
#define CAROUSEL_CY       58  // Vertical center, aligned with price

// ── Price position (centered) ──
#define PRICE_CX          (SCREEN_W / 2)
#define PRICE_CY           50

// ── Non-blocking flash state ──
static bool     flashActive   = false;
static uint8_t  flashZoneId   = 255;
static uint32_t flashStartMs  = 0;
static const uint32_t FLASH_DURATION_MS = 150;

// ── Persistent per-zone sprites (allocated once in dashboardSetup) ──
static LGFX_Sprite sprZ0(&tft);   // Header   480×44
static LGFX_Sprite sprZ1(&tft);   // BTC Hero 480×300
static LGFX_Sprite sprZ2(&tft);   // Lemon    480×120
static bool spritesReady = false;

// ── Dirty zone bitmask (bit 0=Z0, bit 1=Z1, bit 2=Z2) ──
static uint8_t dirtyZones = 0x07;  // All dirty initially

// ── Carousel momentum state (global, accessible from main.cpp) ──
CarouselState btcCarousel     = { 0.0f, 0.0f, false, -1 };
CarouselState dollarCarouselState = { 0.0f, 0.0f, false, -1 };
ChartZoomState chartZoom      = { 1.0f, 1.0f, false };  // Default: no zoom, panned to right

// ── Pair dropdown state ──
static bool    pairDropdownOpen     = false;
static uint8_t pairDropdownSelected = 0;

void updateCarouselPhysics(CarouselState& cs, int maxItems, float dt) {
    if (!cs.animating) return;

    // Friction deceleration
    const float friction = 8.0f;  // items/s^2
    float decel = friction * dt;

    if (fabsf(cs.velocity) <= decel) {
        // Snap to nearest integer
        cs.velocity = 0.0f;
        cs.scrollOffset = roundf(cs.scrollOffset);
        // Clamp
        if (cs.scrollOffset < 0) cs.scrollOffset = 0;
        if (cs.scrollOffset > maxItems - 1) cs.scrollOffset = maxItems - 1;
        cs.animating = false;
    } else {
        // Apply friction
        if (cs.velocity > 0) cs.velocity -= decel;
        else cs.velocity += decel;

        cs.scrollOffset += cs.velocity * dt;

        // Clamp with bounce-back
        if (cs.scrollOffset < -0.3f) {
            cs.scrollOffset = -0.3f;
            cs.velocity = fabsf(cs.velocity) * 0.3f;  // Bounce
        }
        if (cs.scrollOffset > maxItems - 1 + 0.3f) {
            cs.scrollOffset = maxItems - 1 + 0.3f;
            cs.velocity = -fabsf(cs.velocity) * 0.3f;
        }
    }
}

// ── Helper: draw flash border inside a zone sprite ──
static void drawFlashBorderIfActive(LGFX_Sprite& spr, uint8_t zoneId, int h) {
    if (flashActive && flashZoneId == zoneId) {
        spr.drawRoundRect(MARGIN - 1, 0, CARD_W + 2, h + 1, CARD_R, Colors::LEMON_GREEN);
    }
}

// ── Helper: get zone Y/H ──
static bool getZoneBounds(uint8_t zoneId, int& y, int& h) {
    switch (zoneId) {
        case 0: y = Z0_Y; h = Z0_H; return true;
        case 1: y = Z1_Y; h = z1H; return true;
        case 2: y = z2Y; h = z2H; return true;
        default: return false;
    }
}

// Period labels come from BTC_PERIODS[] in config.h
// PERIOD_COUNT replaced by BTC_PERIOD_COUNT

// ── Price flash state (directional color feedback) ──
static bool     priceFlashUp      = true;
static uint32_t priceFlashStartMs = 0;
static bool     priceFlashActive  = false;
static const uint32_t PRICE_FLASH_DURATION_MS = 600;

// ── RGB565 color lerp for price flash ──
static uint16_t blendColor565(uint16_t c1, uint16_t c2, float t) {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
    uint8_t r = r1 + (int)((r2 - r1) * t);
    uint8_t g = g1 + (int)((g2 - g1) * t);
    uint8_t b = b1 + (int)((b2 - b1) * t);
    return (r << 11) | (g << 5) | b;
}

void dashboardFlashPrice(bool up) {
    priceFlashUp = up;
    priceFlashStartMs = millis();
    priceFlashActive = true;
}

// ══════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════

void dashboardSetup() {
    tft.fillScreen(Colors::BG_BASE);

    // Allocate persistent zone sprites in PSRAM (once, never freed)
    sprZ0.setPsram(true);
    sprZ0.setColorDepth(16);
    sprZ0.createSprite(SCREEN_W, Z0_H);

    sprZ1.setPsram(true);
    sprZ1.setColorDepth(16);
    sprZ1.createSprite(SCREEN_W, z1H);

    sprZ2.setPsram(true);
    sprZ2.setColorDepth(16);
    sprZ2.createSprite(SCREEN_W, z2H);

    spritesReady = true;

    // Fill gaps between zones once (static, never need redrawing)
    tft.fillRect(0, Z0_Y + Z0_H, SCREEN_W, Z1_Y - (Z0_Y + Z0_H), Colors::BG_BASE);
    tft.fillRect(0, Z1_Y + z1H, SCREEN_W, z2Y - (Z1_Y + z1H), Colors::BG_BASE);
    tft.fillRect(0, z2Y + z2H, SCREEN_W, SCREEN_H - (z2Y + z2H), Colors::BG_BASE);

    Serial.printf("[Dashboard] Zone sprites allocated: Z0=%dB Z1=%dB Z2=%dB\n",
                  SCREEN_W * Z0_H * 2, SCREEN_W * z1H * 2, SCREEN_W * z2H * 2);
}

// ══════════════════════════════════════════
//  LAYOUT PRESETS
// ══════════════════════════════════════════

uint8_t dashboardGetLayout() {
    return currentLayout;
}

void dashboardSetLayout(uint8_t idx) {
    if (idx > 2) idx = 0;
    currentLayout = idx;

    switch (idx) {
        case 1:  // BTC Focus
            z1H = 370; z2H = 50; break;
        case 2:  // Compact
            z1H = 240; z2H = 180; break;
        default: // Standard
            z1H = 300; z2H = 120; break;
    }
    z2Y = Z1_Y + z1H + GAP;

    // Recreate sprites at new sizes
    if (spritesReady) {
        sprZ1.deleteSprite();
        sprZ2.deleteSprite();

        sprZ1.setPsram(true);
        sprZ1.setColorDepth(16);
        sprZ1.createSprite(SCREEN_W, z1H);

        sprZ2.setPsram(true);
        sprZ2.setColorDepth(16);
        sprZ2.createSprite(SCREEN_W, z2H);

        // Refill gaps
        tft.fillRect(0, Z1_Y + z1H, SCREEN_W, z2Y - (Z1_Y + z1H), Colors::BG_BASE);
        tft.fillRect(0, z2Y + z2H, SCREEN_W, SCREEN_H - (z2Y + z2H), Colors::BG_BASE);

        Serial.printf("[Dashboard] Layout %d: Z1=%d Z2=%d\n", idx, z1H, z2H);
    }
}

// ══════════════════════════════════════════
//  Z0: HEADER (44px)
// ══════════════════════════════════════════

void dashboardDrawHeader(const char* timeStr, bool offline, bool wsConnected) {
    sprZ0.fillSprite(Colors::BG_BASE);

    // ── Toast overlay: replaces normal header content while active ──
    if (isToastActive()) {
        int barH = 36;
        int barX = 20;
        int barW = SCREEN_W - 40;
        sprZ0.fillSmoothRoundRect(barX, (Z0_H - barH) / 2, barW, barH, 10, Colors::BG_SURFACE);
        sprZ0.drawRoundRect(barX, (Z0_H - barH) / 2, barW, barH, 10, Colors::CARD_BORDER);
        sprZ0.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_SURFACE);
        sprZ0.setTextDatum(lgfx::middle_center);
        sprZ0.drawString(getToastMessage(), SCREEN_W / 2, Z0_H / 2, &Satoshi12);

        sprZ0.pushSprite(0, Z0_Y);
        dirtyZones |= (1 << 0);
        return;
    }

    // Header left side: isotipo + greeting when paired, or full imagotipo
    int logoX = MARGIN;
    int logoY = (Z0_H - 28) / 2;
    if (supabaseGetPairingState() == PAIRING_PAIRED && supabaseGetLemonTag()[0] != '\0') {
        // Paired: isotipo (28x28) + "Hola @tag"
        drawLemonIsotipo28(sprZ0, logoX, logoY);
        char greetBuf[48];
        snprintf(greetBuf, sizeof(greetBuf), "Hola @%s", supabaseGetLemonTag());
        sprZ0.setTextColor(Colors::LEMON_GREEN, Colors::BG_BASE);
        sprZ0.setTextDatum(lgfx::middle_left);
        sprZ0.drawString(greetBuf, logoX + 34, Z0_H / 2, &Satoshi12);
    } else {
        // Default: full imagotipo (122x28 — icon + LEMON wordmark)
        drawLemonImagotipo122(sprZ0, logoX, logoY);
    }

    if (offline) {
        int badgeW = 90, badgeH = 24;
        int badgeX = SCREEN_W - badgeW - MARGIN;
        int badgeY = (Z0_H - badgeH) / 2;
        sprZ0.fillSmoothRoundRect(badgeX, badgeY, badgeW, badgeH, 8, Colors::NEGATIVE);
        sprZ0.setTextColor(Colors::TEXT_PRIMARY, Colors::NEGATIVE);
        sprZ0.setTextDatum(lgfx::middle_center);
        sprZ0.drawString("OFFLINE", badgeX + badgeW / 2, badgeY + badgeH / 2, &Satoshi12);
    } else {
        int timeX = SCREEN_W - MARGIN;
        sprZ0.setTextColor(Colors::LEMON_GREEN, Colors::BG_BASE);
        sprZ0.setTextDatum(lgfx::middle_right);
        sprZ0.drawString(timeStr, timeX, Z0_H / 2, &fonts::Orbitron_Light_24);
    }

    // Separator line
    sprZ0.drawFastHLine(MARGIN, Z0_H - 1, CARD_W, Colors::DIVIDER);

    sprZ0.pushSprite(0, Z0_Y);
    dirtyZones |= (1 << 0);
}

// ── Helper: draw vertical carousel (5 visible items, momentum-aware) ──
static void drawCarousel(LGFX_Sprite& spr, uint8_t selectedPeriod) {
    int cx = CAROUSEL_X + CAROUSEL_W / 2;
    int step = CAROUSEL_ITEM_H + CAROUSEL_ITEM_GAP;

    // Fractional offset from carousel state for smooth scrolling
    float offset = btcCarousel.animating
                   ? (btcCarousel.scrollOffset - (float)selectedPeriod)
                   : 0.0f;

    // Selected item background (always at center)
    int selY = CAROUSEL_CY - CAROUSEL_ITEM_H / 2;
    spr.fillSmoothRoundRect(CAROUSEL_X, selY, CAROUSEL_W, CAROUSEL_ITEM_H, 6, Colors::BG_ELEVATED);

    // Divider lines (iOS picker band)
    spr.drawFastHLine(CAROUSEL_X, selY - 1, CAROUSEL_W, Colors::DIVIDER);
    spr.drawFastHLine(CAROUSEL_X, selY + CAROUSEL_ITEM_H, CAROUSEL_W, Colors::DIVIDER);

    // Green accent dot on left of selected
    spr.fillSmoothCircle(CAROUSEL_X + 8, CAROUSEL_CY, 3, Colors::LEMON_GREEN);

    // Draw selected label
    spr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_ELEVATED);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString(BTC_PERIODS[selectedPeriod].label, cx + 4, CAROUSEL_CY, &Satoshi12);

    // Draw up to 2 items above and 2 below (5 visible total)
    for (int d = 1; d <= 2; d++) {
        // Above
        int idxAbove = (int)selectedPeriod - d;
        if (idxAbove >= 0) {
            int itemCY = CAROUSEL_CY - d * step - (int)(offset * step);
            uint16_t color = (d == 1) ? Colors::TEXT_SECONDARY : Colors::TEXT_TERTIARY;
            spr.setTextColor(color, Colors::BG_CARD);
            spr.setTextDatum(lgfx::middle_center);
            spr.drawString(BTC_PERIODS[idxAbove].label, cx, itemCY, &Satoshi12);
        }

        // Below
        int idxBelow = (int)selectedPeriod + d;
        if (idxBelow < BTC_PERIOD_COUNT) {
            int itemCY = CAROUSEL_CY + d * step - (int)(offset * step);
            uint16_t color = (d == 1) ? Colors::TEXT_SECONDARY : Colors::TEXT_TERTIARY;
            spr.setTextColor(color, Colors::BG_CARD);
            spr.setTextDatum(lgfx::middle_center);
            spr.drawString(BTC_PERIODS[idxBelow].label, cx, itemCY, &Satoshi12);
        }
    }
}

// ── Helper: draw pair dropdown overlay (dark card with 5 items) ──
static void drawPairDropdown(LGFX_Sprite& spr, uint8_t currentPair) {
    int totalH = DROPDOWN_PAD * 2 + BTC_PAIR_COUNT * DROPDOWN_ITEM_H;

    // Dark overlay background with rounded corners
    spr.fillSmoothRoundRect(DROPDOWN_X, DROPDOWN_Y, DROPDOWN_W, totalH, DROPDOWN_R, Colors::BG_OVERLAY);
    spr.drawRoundRect(DROPDOWN_X, DROPDOWN_Y, DROPDOWN_W, totalH, DROPDOWN_R, Colors::CARD_BORDER);

    for (int i = 0; i < BTC_PAIR_COUNT; i++) {
        int itemY = DROPDOWN_Y + DROPDOWN_PAD + i * DROPDOWN_ITEM_H;
        int itemCY = itemY + DROPDOWN_ITEM_H / 2;

        if (i == currentPair) {
            // Selected item: subtle highlight background
            spr.fillSmoothRoundRect(DROPDOWN_X + 4, itemY + 2, DROPDOWN_W - 8, DROPDOWN_ITEM_H - 4,
                                     6, Colors::BG_ELEVATED);
            // Green accent dot
            spr.fillSmoothCircle(DROPDOWN_X + 16, itemCY, 3, Colors::LEMON_GREEN);
            spr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_ELEVATED);
        } else {
            spr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_OVERLAY);
        }

        // Pair label (e.g. "BTC/USD")
        spr.setTextDatum(lgfx::middle_left);
        spr.drawString(BTC_PAIRS[i].pairLabel, DROPDOWN_X + 26, itemCY, &Satoshi12);

        // Divider line (except after last item)
        if (i < BTC_PAIR_COUNT - 1) {
            spr.drawFastHLine(DROPDOWN_X + 10, itemY + DROPDOWN_ITEM_H - 1,
                              DROPDOWN_W - 20, Colors::DIVIDER);
        }
    }
}

// ── Direct-to-framebuffer time update (fixed-width digits, per-cell rendering) ──
void dashboardUpdateTimeDirect(const char* timeStr) {
    if (!spritesReady) return;

    // Measure max digit width for Orbitron (cached)
    static int maxDigitW_clock = 0;
    if (maxDigitW_clock == 0) {
        char d[2] = {0, 0};
        for (int i = 0; i <= 9; i++) {
            d[0] = '0' + i;
            int w = tft.textWidth(d, &fonts::Orbitron_Light_24);
            if (w > maxDigitW_clock) maxDigitW_clock = w;
        }
    }

    // Calculate total fixed width (digits = maxDigitW, colons = natural)
    int len = strlen(timeStr);
    int totalW = 0;
    for (int i = 0; i < len; i++) {
        if (timeStr[i] >= '0' && timeStr[i] <= '9') {
            totalW += maxDigitW_clock;
        } else {
            char c[2] = { timeStr[i], 0 };
            totalW += tft.textWidth(c, &fonts::Orbitron_Light_24);
        }
    }

    int timeRightX = SCREEN_W - MARGIN;
    int startX = timeRightX - totalW;  // right-aligned
    int cy_spr = Z0_H / 2;
    int cy_scr = Z0_Y + cy_spr;
    int cellH = 30;
    int cellY_spr = cy_spr - cellH / 2;
    int cellY_scr = cy_scr - cellH / 2;

    // Update sprZ0 for consistency with future full pushes
    sprZ0.fillRect(startX, cellY_spr, totalW, cellH, Colors::BG_BASE);
    int sx = startX;
    sprZ0.setTextColor(Colors::LEMON_GREEN);
    sprZ0.setTextDatum(lgfx::middle_center);
    for (int i = 0; i < len; i++) {
        char c[2] = { timeStr[i], 0 };
        int cellW = (timeStr[i] >= '0' && timeStr[i] <= '9')
                    ? maxDigitW_clock
                    : sprZ0.textWidth(c, &fonts::Orbitron_Light_24);
        sprZ0.drawString(c, sx + cellW / 2, cy_spr, &fonts::Orbitron_Light_24);
        sx += cellW;
    }

    // Draw directly to framebuffer — per-cell fill+draw (no visible flicker)
    displayWaitVSync();
    int x = startX;
    tft.setTextColor(Colors::LEMON_GREEN);
    tft.setTextDatum(lgfx::middle_center);
    for (int i = 0; i < len; i++) {
        char c[2] = { timeStr[i], 0 };
        int cellW = (timeStr[i] >= '0' && timeStr[i] <= '9')
                    ? maxDigitW_clock
                    : tft.textWidth(c, &fonts::Orbitron_Light_24);
        tft.fillRect(x, cellY_scr, cellW, cellH, Colors::BG_BASE);
        tft.drawString(c, x + cellW / 2, cy_scr, &fonts::Orbitron_Light_24);
        x += cellW;
    }
}

// ── Direct-to-framebuffer price update (no pushSprite) ──
void dashboardUpdatePriceDirect(const BtcPrice& btc, uint8_t selectedPair) {
    if (!spritesReady || !btc.valid) return;

    char priceBuf[24];
    const PairDef& pair = BTC_PAIRS[selectedPair];
    formatPairPrice(priceBuf, sizeof(priceBuf), btc.usd, pair.prefix, pair.suffix, pair.decimals);

    uint16_t priceColor = Colors::TEXT_PRIMARY;
    if (priceFlashActive) {
        uint32_t elapsed = millis() - priceFlashStartMs;
        if (elapsed < PRICE_FLASH_DURATION_MS) {
            float progress = (float)elapsed / PRICE_FLASH_DURATION_MS;
            uint16_t flashColor = priceFlashUp ? Colors::POSITIVE : Colors::NEGATIVE;
            priceColor = blendColor565(flashColor, Colors::TEXT_PRIMARY, progress);
        } else {
            priceFlashActive = false;
        }
    }

    // Skip direct price update when dropdown is open (would overwrite overlay)
    if (pairDropdownOpen) return;

    // Price area in sprite coords (between pair label and period carousel)
    const int STRIP_Y = 30;
    const int STRIP_H = 44;
    const int STRIP_X = MARGIN + CARD_PAD;
    const int STRIP_W = CAROUSEL_X - STRIP_X - 2;

    // Use smaller font for non-USD pairs
    const lgfx::IFont* priceFont = (selectedPair == 0) ? &SatoshiBold40 : &SatoshiBold24;

    // Update sprZ1 to stay in sync
    sprZ1.fillRect(STRIP_X, STRIP_Y, STRIP_W, STRIP_H, Colors::BG_CARD);
    drawFixedWidthPrice(sprZ1, priceBuf, PRICE_CX, PRICE_CY, priceFont, priceColor);

    // Write directly to framebuffer (small area, fits in VBlank)
    displayWaitVSync();
    tft.fillRect(STRIP_X, Z1_Y + STRIP_Y, STRIP_W, STRIP_H, Colors::BG_CARD);
    drawFixedWidthPrice(tft, priceBuf, PRICE_CX, Z1_Y + PRICE_CY, priceFont, priceColor);
}

// ══════════════════════════════════════════
//  Z1: BTC HERO + CHART (300px)
// ══════════════════════════════════════════

void dashboardDrawBtcHero(const BtcPrice& btc, const SparklineData& spark, uint8_t selectedPeriod,
                          const float* periodChanges, ChartStyle chartStyle, const OhlcData* ohlc,
                          uint8_t selectedPair) {
    sprZ1.fillSprite(Colors::BG_BASE);

    // Hero card (accent border, radius 20)
    drawHeroCard(sprZ1, MARGIN, 0, CARD_W, z1H);

    const PairDef& pair = BTC_PAIRS[selectedPair];

    if (!btc.valid) {
        char loadBuf[24];
        snprintf(loadBuf, sizeof(loadBuf), "%s...", pair.pairLabel);
        drawCentered(sprZ1, loadBuf, z1H / 2 - 10,
                     &SatoshiMedium18, Colors::TEXT_SECONDARY);
        sprZ1.pushSprite(0, Z1_Y);
        dirtyZones |= (1 << 1);
        return;
    }

    // ── Top row: pair label (tappable — opens dropdown) ──
    sprZ1.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
    sprZ1.setTextDatum(lgfx::top_left);
    sprZ1.drawString(pair.pairLabel, PAIR_LABEL_X, PAIR_LABEL_Y, &SatoshiMedium18);
    // Down-chevron triangle (▼) next to label
    {
        int chevX = PAIR_LABEL_X + sprZ1.textWidth(pair.pairLabel, &SatoshiMedium18) + 8;
        int chevY = PAIR_LABEL_Y + 7;
        sprZ1.fillTriangle(chevX, chevY, chevX + 10, chevY, chevX + 5, chevY + 6, Colors::TEXT_SECONDARY);
    }

    // ── Main price (no glow, centered) ──
    // Use smaller font for non-USD pairs (longer text like "29.45 ETH")
    const lgfx::IFont* priceFont = (selectedPair == 0) ? &SatoshiBold40 : &SatoshiBold24;

    char priceBuf[24];
    formatPairPrice(priceBuf, sizeof(priceBuf), btc.usd, pair.prefix, pair.suffix, pair.decimals);

    // Price flash: blend from green/red toward white over 600ms
    uint16_t priceColor = Colors::TEXT_PRIMARY;
    if (priceFlashActive) {
        uint32_t elapsed = millis() - priceFlashStartMs;
        if (elapsed < PRICE_FLASH_DURATION_MS) {
            float progress = (float)elapsed / PRICE_FLASH_DURATION_MS;
            uint16_t flashColor = priceFlashUp ? Colors::POSITIVE : Colors::NEGATIVE;
            priceColor = blendColor565(flashColor, Colors::TEXT_PRIMARY, progress);
        } else {
            priceFlashActive = false;
        }
    }

    drawFixedWidthPrice(sprZ1, priceBuf, PRICE_CX, PRICE_CY, priceFont, priceColor);

    // ── Selected period change text below price ──
    if (periodChanges) {
        float change = periodChanges[selectedPeriod];
        if (!isnan(change)) {
            bool pos = change >= 0;
            char changeBuf[24];
            snprintf(changeBuf, sizeof(changeBuf), "%s%.1f%% %s",
                     pos ? "+" : "", change, BTC_PERIODS[selectedPeriod].label);
            uint16_t changeColor = pos ? Colors::POSITIVE : Colors::NEGATIVE;
            sprZ1.setTextColor(changeColor);
            sprZ1.setTextDatum(lgfx::middle_center);
            sprZ1.drawString(changeBuf, PRICE_CX, 88, &Satoshi12);
        }
    }

    // ── Period carousel (right side) ──
    drawCarousel(sprZ1, selectedPeriod);

    // ── Chart area (bottom portion) ──
    int chartX = MARGIN + CARD_PAD;
    int chartY = 100;
    int chartW = CARD_W - 2 * CARD_PAD;
    int chartH = z1H - chartY - 10;

    if (chartH > 10) {
        bool zoomed = chartZoom.active && chartZoom.zoomLevel > 1.01f;

        switch (chartStyle) {
            case CHART_CANDLE:
                if (ohlc && ohlc->valid && ohlc->count >= 2) {
                    drawCandlestick(sprZ1, chartX, chartY, chartW, chartH, *ohlc);
                } else if (spark.valid && spark.count >= 2) {
                    if (zoomed) {
                        drawSparklineZoomed(sprZ1, chartX, chartY, chartW, chartH,
                                            spark, Colors::CHART_LINE, Colors::CHART_FILL,
                                            chartZoom.zoomLevel, chartZoom.panOffset);
                    } else {
                        drawSparkline(sprZ1, chartX, chartY, chartW, chartH,
                                      spark, Colors::CHART_LINE, Colors::CHART_FILL);
                    }
                }
                break;

            case CHART_MARKERS:
                if (spark.valid && spark.count >= 2) {
                    if (zoomed) {
                        drawSparklineZoomed(sprZ1, chartX, chartY, chartW, chartH,
                                            spark, Colors::CHART_LINE, Colors::CHART_FILL,
                                            chartZoom.zoomLevel, chartZoom.panOffset);
                    } else {
                        drawSparkline(sprZ1, chartX, chartY, chartW, chartH,
                                      spark, Colors::CHART_LINE, Colors::CHART_FILL);
                    }
                    if (!zoomed) {
                        drawChartMarkers(sprZ1, chartX, chartY, chartW, chartH,
                                        spark, btc.ath);
                    }
                }
                break;

            case CHART_LINE:
            default:
                if (spark.valid && spark.count >= 2) {
                    if (zoomed) {
                        drawSparklineZoomed(sprZ1, chartX, chartY, chartW, chartH,
                                            spark, Colors::CHART_LINE, Colors::CHART_FILL,
                                            chartZoom.zoomLevel, chartZoom.panOffset);
                    } else {
                        drawSparkline(sprZ1, chartX, chartY, chartW, chartH,
                                      spark, Colors::CHART_LINE, Colors::CHART_FILL);
                    }
                }
                break;
        }

    }

    // ── Zoom level indicator (pill badge, top-right of chart) ──
    if (chartZoom.active && chartZoom.zoomLevel > 1.01f && chartH > 10) {
        char zoomBuf[8];
        snprintf(zoomBuf, sizeof(zoomBuf), "%.1fx", chartZoom.zoomLevel);
        int tw = sprZ1.textWidth(zoomBuf, &Satoshi9) + 10;
        int th = 16;
        int bx = chartX + chartW - tw - 2;
        int by = chartY + 4;
        sprZ1.fillSmoothRoundRect(bx, by, tw, th, 4, Colors::BG_ELEVATED);
        sprZ1.setTextColor(Colors::LEMON_GREEN, Colors::BG_ELEVATED);
        sprZ1.setTextDatum(lgfx::middle_center);
        sprZ1.drawString(zoomBuf, bx + tw / 2, by + th / 2, &Satoshi9);
    }

    // ── Pair dropdown overlay (drawn on top of chart if open) ──
    if (pairDropdownOpen) {
        drawPairDropdown(sprZ1, pairDropdownSelected);
    }

    // Flash border (drawn last, on top of everything)
    drawFlashBorderIfActive(sprZ1, 1, z1H);

    displayWaitVSync();
    sprZ1.pushSprite(0, Z1_Y);
    dirtyZones |= (1 << 1);
}

// ── Price-only partial update ──
// Only redraws the price text strip, clipped between pair carousel and period carousel.
void dashboardDrawPriceOnly(const BtcPrice& btc, uint8_t selectedPair) {
    if (!spritesReady || !btc.valid) return;

    // Skip price-only update when dropdown is open
    if (pairDropdownOpen) return;

    // Generous strip covering full SatoshiBold40 text height around PRICE_CY=50
    const int STRIP_Y = 30;
    const int STRIP_H = 50;   // covers y=30..80, avoids clipping pair label
    const int CLIP_X  = MARGIN + CARD_PAD;
    const int CLIP_W  = CAROUSEL_X - CLIP_X - 2;

    // Clip sprite drawing to price strip, between carousels
    sprZ1.setClipRect(CLIP_X, STRIP_Y, CLIP_W, STRIP_H);

    // Clear the strip area with card background
    sprZ1.fillRect(CLIP_X, STRIP_Y, CLIP_W, STRIP_H, Colors::BG_CARD);

    const PairDef& pair = BTC_PAIRS[selectedPair];
    char priceBuf[24];
    formatPairPrice(priceBuf, sizeof(priceBuf), btc.usd, pair.prefix, pair.suffix, pair.decimals);

    uint16_t priceColor = Colors::TEXT_PRIMARY;
    if (priceFlashActive) {
        uint32_t elapsed = millis() - priceFlashStartMs;
        if (elapsed < PRICE_FLASH_DURATION_MS) {
            float progress = (float)elapsed / PRICE_FLASH_DURATION_MS;
            uint16_t flashColor = priceFlashUp ? Colors::POSITIVE : Colors::NEGATIVE;
            priceColor = blendColor565(flashColor, Colors::TEXT_PRIMARY, progress);
        } else {
            priceFlashActive = false;
        }
    }

    const lgfx::IFont* priceFont = (selectedPair == 0) ? &SatoshiBold40 : &SatoshiBold24;
    drawFixedWidthPrice(sprZ1, priceBuf, PRICE_CX, PRICE_CY, priceFont, priceColor);

    sprZ1.clearClipRect();

    // VSync + push only the price strip (much less PSRAM bus contention than full Z1)
    displayWaitVSync();
    tft.setClipRect(CLIP_X, Z1_Y + STRIP_Y, CLIP_W, STRIP_H);
    sprZ1.pushSprite(0, Z1_Y);
    tft.clearClipRect();
}

// ══════════════════════════════════════════
//  Z2: DOLAR DIGITAL (120px)
// ══════════════════════════════════════════

// Dollar period labels come from DOLLAR_PERIODS[] in config.h

#define DCAR_W          84
#define DCAR_ITEM_H     28
#define DCAR_ITEM_GAP    2
#define DCAR_X          (MARGIN + CARD_W - CARD_PAD - DCAR_W)
#define DCAR_CY         50

// Dynamic CY for dollar carousel — centered for compact zone
static int dollarCarouselCY() {
    return (z2H <= 60) ? z2H / 2 : DCAR_CY;
}

static void drawDollarCarousel(LGFX_Sprite& spr, uint8_t selected) {
    int cy = dollarCarouselCY();
    int cx = DCAR_X + DCAR_W / 2;
    int step = DCAR_ITEM_H + DCAR_ITEM_GAP;

    int selY = cy - DCAR_ITEM_H / 2;
    spr.fillSmoothRoundRect(DCAR_X, selY, DCAR_W, DCAR_ITEM_H, 6, Colors::BG_ELEVATED);

    spr.drawFastHLine(DCAR_X, selY - 1, DCAR_W, Colors::DIVIDER);
    spr.drawFastHLine(DCAR_X, selY + DCAR_ITEM_H, DCAR_W, Colors::DIVIDER);

    spr.fillSmoothCircle(DCAR_X + 8, cy, 3, Colors::NEBULA);

    spr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_ELEVATED);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString(DOLLAR_PERIODS[selected].label, cx + 4, cy, &Satoshi12);

    // Draw up to 2 items above and 2 below (5 visible total)
    for (int d = 1; d <= 2; d++) {
        int idxAbove = (int)selected - d;
        if (idxAbove >= 0) {
            int itemCY = cy - d * step;
            uint16_t color = (d == 1) ? Colors::TEXT_SECONDARY : Colors::TEXT_TERTIARY;
            spr.setTextColor(color, Colors::BG_CARD);
            spr.setTextDatum(lgfx::middle_center);
            spr.drawString(DOLLAR_PERIODS[idxAbove].label, cx, itemCY, &Satoshi12);
        }

        int idxBelow = (int)selected + d;
        if (idxBelow < DOLLAR_PERIOD_COUNT) {
            int itemCY = cy + d * step;
            uint16_t color = (d == 1) ? Colors::TEXT_SECONDARY : Colors::TEXT_TERTIARY;
            spr.setTextColor(color, Colors::BG_CARD);
            spr.setTextDatum(lgfx::middle_center);
            spr.drawString(DOLLAR_PERIODS[idxBelow].label, cx, itemCY, &Satoshi12);
        }
    }
}

void dashboardDrawLemonDollar(const LemonPrice& lemon, const SparklineData* lemonSpark,
                              uint8_t dollarPeriod, ChartStyle dollarChartStyle,
                              float dollarChange) {
    sprZ2.fillSprite(Colors::BG_BASE);

    drawGlassCard(sprZ2, MARGIN, 0, CARD_W, z2H, CARD_R);

    if (!lemon.valid) {
        drawCentered(sprZ2, "USDT/ARS...", z2H / 2 - 6,
                     &Satoshi12, Colors::TEXT_SECONDARY);
        sprZ2.pushSprite(0, z2Y);
        dirtyZones |= (1 << 2);
        return;
    }

    // ── Adaptive layout based on zone height ──
    bool compact = (z2H <= 60);
    int labelY  = compact ? 8  : 10;
    int priceY  = compact ? 30 : 40;
    const lgfx::IFont* priceFont = compact ? &SatoshiMedium18 : &SatoshiBold24;

    // ── "USDT/ARS" top-left ──
    sprZ2.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
    sprZ2.setTextDatum(lgfx::top_left);
    sprZ2.drawString("USDT/ARS", MARGIN + CARD_PAD, labelY, compact ? &Satoshi9 : &Satoshi12);

    // ── Price + % change (left of carousel) ──
    float avg = (lemon.bid + lemon.ask) / 2.0f;
    char avgBuf[20];
    formatArsPrice(avgBuf, sizeof(avgBuf), avg);

    // Compute % change from sparkline only if no override was provided
    if (isnan(dollarChange) && lemonSpark && lemonSpark->valid && lemonSpark->count >= 2) {
        float first = lemonSpark->points[0];
        float last = lemonSpark->points[lemonSpark->count - 1];
        if (first > 0 && last > 0 && first > last * 0.01f) {
            dollarChange = ((last - first) / first) * 100.0f;
        }
    }

    sprZ2.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
    sprZ2.setTextDatum(lgfx::middle_center);
    sprZ2.drawString(avgBuf, PRICE_CX, priceY, priceFont);

    // ── % change (skip in compact layout — no room) ──
    if (!compact && !isnan(dollarChange) && fabsf(dollarChange) < 1000.0f) {
        char changeBuf[24];
        if (fabsf(dollarChange) >= 100.0f) {
            snprintf(changeBuf, sizeof(changeBuf), "%s%.0f%% %s",
                     dollarChange >= 0 ? "+" : "", dollarChange,
                     DOLLAR_PERIODS[dollarPeriod].label);
        } else {
            snprintf(changeBuf, sizeof(changeBuf), "%s%.1f%% %s",
                     dollarChange >= 0 ? "+" : "", dollarChange,
                     DOLLAR_PERIODS[dollarPeriod].label);
        }
        uint16_t changeColor = dollarChange >= 0 ? Colors::POSITIVE : Colors::NEGATIVE;
        sprZ2.setTextColor(changeColor, Colors::BG_CARD);
        sprZ2.setTextDatum(lgfx::middle_center);
        sprZ2.drawString(changeBuf, PRICE_CX, 66, &Satoshi12);
    }

    // ── Carousel (right side) ──
    drawDollarCarousel(sprZ2, dollarPeriod);

    // ── Sparkline (bottom, stops before carousel — only if enough room) ──
    if (!compact && lemonSpark && lemonSpark->valid && lemonSpark->count >= 2) {
        int chartX = MARGIN + CARD_PAD;
        int chartY = 82;
        int chartW = DCAR_X - chartX - 8;
        int chartH = z2H - chartY - 6;

        if (chartH > 10) {
            drawSparkline(sprZ2, chartX, chartY, chartW, chartH,
                          *lemonSpark, Colors::NEBULA, Colors::NEBULA_FILL);
            if (dollarChartStyle == CHART_MARKERS) {
                drawChartMarkers(sprZ2, chartX, chartY, chartW, chartH,
                                 *lemonSpark, 0.0f);  // no ATH for ARS
            }
        }
    }

    // Flash border (drawn last, on top of everything)
    drawFlashBorderIfActive(sprZ2, 2, z2H);

    displayWaitVSync();
    sprZ2.pushSprite(0, z2Y);
    dirtyZones |= (1 << 2);
}

int8_t dashboardHitTestDollarCarousel(int16_t x, int16_t y) {
    if (x < DCAR_X || x >= DCAR_X + DCAR_W) return 0;

    int cy = dollarCarouselCY();
    int sprY = y - z2Y;
    int step = DCAR_ITEM_H + DCAR_ITEM_GAP;
    int selTop = cy - DCAR_ITEM_H / 2;
    int selBot = cy + DCAR_ITEM_H / 2;

    if (sprY >= selTop - 2 * step && sprY < selTop - step) return -2;
    if (sprY >= selTop - step && sprY < selTop) return -1;
    if (sprY >= selBot && sprY < selBot + step) return +1;
    if (sprY >= selBot + step && sprY < selBot + 2 * step) return +2;

    return 0;
}

// ── Pair dropdown functions ──

bool dashboardIsPairDropdownOpen() {
    return pairDropdownOpen;
}

void dashboardOpenPairDropdown(uint8_t currentPair) {
    pairDropdownOpen = true;
    pairDropdownSelected = currentPair;
}

void dashboardClosePairDropdown() {
    pairDropdownOpen = false;
}

int8_t dashboardHitTestPairDropdown(int16_t x, int16_t y) {
    if (!pairDropdownOpen) return -1;

    int sprY = y - Z1_Y;
    int sprX = x;

    // Check if tap is within dropdown bounds
    int totalH = DROPDOWN_PAD * 2 + BTC_PAIR_COUNT * DROPDOWN_ITEM_H;
    if (sprX < DROPDOWN_X || sprX >= DROPDOWN_X + DROPDOWN_W) return -1;
    if (sprY < DROPDOWN_Y || sprY >= DROPDOWN_Y + totalH) return -1;

    // Determine which item was tapped
    int relY = sprY - DROPDOWN_Y - DROPDOWN_PAD;
    if (relY < 0) return -1;
    int idx = relY / DROPDOWN_ITEM_H;
    if (idx >= 0 && idx < BTC_PAIR_COUNT) return (int8_t)idx;

    return -1;
}

bool dashboardHitTestPairLabel(int16_t x, int16_t y) {
    int sprY = y - Z1_Y;
    return (x >= PAIR_LABEL_X && x < PAIR_LABEL_X + PAIR_LABEL_W &&
            sprY >= PAIR_LABEL_Y && sprY < PAIR_LABEL_Y + PAIR_LABEL_H);
}

// ══════════════════════════════════════════
//  FULL REDRAW
// ══════════════════════════════════════════

void dashboardDrawAll(const char* timeStr,
                      const BtcPrice& btc, const SparklineData& spark,
                      uint8_t selectedPeriod,
                      const LemonPrice& lemon,
                      bool offline, bool wsConnected,
                      const float* periodChanges,
                      ChartStyle chartStyle, const OhlcData* ohlc,
                      const SparklineData* lemonSpark,
                      uint8_t dollarPeriod,
                      ChartStyle dollarChartStyle,
                      float dollarChange) {
    dashboardDrawHeader(timeStr, offline, wsConnected);
    dashboardDrawBtcHero(btc, spark, selectedPeriod, periodChanges, chartStyle, ohlc);
    dashboardDrawLemonDollar(lemon, lemonSpark, dollarPeriod, dollarChartStyle, dollarChange);
}

// ══════════════════════════════════════════
//  DOUBLE-BUFFER SYNC
// ══════════════════════════════════════════

void dashboardSyncDrawBuffer() {
    if (!spritesReady) return;

    uint8_t dz = dirtyZones;
    dirtyZones = 0;  // Reset for next frame

    // Only push zones that were actually modified this frame.
    // Gaps are filled once in dashboardSetup() — no need to refill here.
    if (dz & (1 << 0)) {
        sprZ0.pushSprite(0, Z0_Y);
    }
    if (dz & (1 << 1)) {
        sprZ1.pushSprite(0, Z1_Y);
    }
    if (dz & (1 << 2)) {
        sprZ2.pushSprite(0, z2Y);
    }
}

void dashboardMarkDirty(uint8_t zoneId) {
    if (zoneId < 3) dirtyZones |= (1 << zoneId);
}

void dashboardMarkAllDirty() {
    dirtyZones = 0x07;
}

void dashboardFillGaps() {
    displayWaitVSync();
    // Header-Z1 gap
    tft.fillRect(0, Z0_Y + Z0_H, SCREEN_W, Z1_Y - (Z0_Y + Z0_H), Colors::BG_BASE);
    // Z1-Z2 gap
    tft.fillRect(0, Z1_Y + z1H, SCREEN_W, z2Y - (Z1_Y + z1H), Colors::BG_BASE);
    // Below Z2
    tft.fillRect(0, z2Y + z2H, SCREEN_W, SCREEN_H - (z2Y + z2H), Colors::BG_BASE);
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

// ── Helper: RGB565 color lerp (for boot animation) ──
static uint16_t lerpColor565(uint16_t c1, uint16_t c2, float t) {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
    uint8_t r = r1 + (int)((r2 - r1) * t);
    uint8_t g = g1 + (int)((g2 - g1) * t);
    uint8_t b = b1 + (int)((b2 - b1) * t);
    return (r << 11) | (g << 5) | b;
}

// ── Helper: convert RGB565 to grayscale RGB565 ──
static uint16_t toGray565(uint16_t c) {
    uint8_t r = (c >> 11) & 0x1F;
    uint8_t g = (c >> 5)  & 0x3F;
    uint8_t b = c & 0x1F;
    // Luminance approximation (scale to 5/6/5 bit ranges)
    uint8_t gray5 = (r * 77 + (g >> 1) * 150 + b * 29) >> 8;  // 5-bit
    uint8_t gray6 = gray5 << 1;
    return (gray5 << 11) | (gray6 << 5) | gray5;
}

void dashboardDrawLoading(LoadPhase phase) {
    if (phase == LOAD_LOGO) {
        tft.fillScreen(Colors::BG_BASE);

        // ── Gray → Color logo fade animation (1.5s at 30fps) ──
        {
            // Draw color logo into a temp sprite
            LGFX_Sprite colorSpr(&tft);
            colorSpr.setPsram(true);
            colorSpr.setColorDepth(16);
            colorSpr.createSprite(LOAD_LOGO_W, LOAD_LOGO_H);
            colorSpr.fillSprite(Colors::BG_BASE);
            drawLemonImagotipo244(colorSpr, 0, 0);

            // Create grayscale version
            LGFX_Sprite graySpr(&tft);
            graySpr.setPsram(true);
            graySpr.setColorDepth(16);
            graySpr.createSprite(LOAD_LOGO_W, LOAD_LOGO_H);
            for (int py = 0; py < LOAD_LOGO_H; py++) {
                for (int px = 0; px < LOAD_LOGO_W; px++) {
                    uint16_t c = colorSpr.readPixel(px, py);
                    graySpr.drawPixel(px, py, toGray565(c));
                }
            }

            // Blend sprite for output
            LGFX_Sprite blendSpr(&tft);
            blendSpr.setPsram(true);
            blendSpr.setColorDepth(16);
            blendSpr.createSprite(LOAD_LOGO_W, LOAD_LOGO_H);

            // Animate: left-to-right color sweep over 2s
            const int FADE_FRAMES = 60;  // 2s at 30fps
            const float FRONT_W = 0.20f; // Width of transition zone (fraction of logo)

            for (int f = 0; f < FADE_FRAMES; f++) {
                float progress = (float)f / (FADE_FRAMES - 1);
                // Ease-out quad for smooth deceleration at end
                float ease = 1.0f - (1.0f - progress) * (1.0f - progress);
                // Front position sweeps from -FRONT_W to 1.0
                float frontPos = -FRONT_W + ease * (1.0f + FRONT_W);

                for (int py = 0; py < LOAD_LOGO_H; py++) {
                    for (int px = 0; px < LOAD_LOGO_W; px++) {
                        float xNorm = (float)px / (LOAD_LOGO_W - 1);
                        float pixelT;
                        if (xNorm <= frontPos) {
                            pixelT = 1.0f;  // Fully colored
                        } else if (xNorm >= frontPos + FRONT_W) {
                            pixelT = 0.0f;  // Still gray
                        } else {
                            pixelT = 1.0f - (xNorm - frontPos) / FRONT_W;
                        }
                        uint16_t gray = graySpr.readPixel(px, py);
                        uint16_t color = colorSpr.readPixel(px, py);
                        blendSpr.drawPixel(px, py, lerpColor565(gray, color, pixelT));
                    }
                }

                displayWaitVSync();
                blendSpr.pushSprite(LOAD_LOGO_X, LOAD_LOGO_Y);
                delay(33);
            }

            // Final: push full color version
            colorSpr.pushSprite(LOAD_LOGO_X, LOAD_LOGO_Y);

            graySpr.deleteSprite();
            blendSpr.deleteSprite();
            colorSpr.deleteSprite();
        }

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
    // Only set flags — the green border is drawn inside zone sprites
    // during the next dashboardDrawBtcHero / dashboardDrawLemonDollar call
    flashActive   = true;
    flashZoneId   = zoneId;
    flashStartMs  = millis();
}

void dashboardUpdateFlash() {
    if (!flashActive) return;

    if (millis() - flashStartMs >= FLASH_DURATION_MS) {
        // Flash expired — just clear flags.
        // The next zone redraw will naturally not draw the border.
        flashActive  = false;
        flashZoneId  = 255;
    }
}

// ══════════════════════════════════════════
//  ZONE HIT-TEST
// ══════════════════════════════════════════

static uint8_t dashHitTestZone(int16_t y) {
    if (y >= Z0_Y && y < Z0_Y + Z0_H) return 0;
    if (y >= Z1_Y && y < Z1_Y + z1H) return 1;
    if (y >= z2Y && y < z2Y + z2H) return 2;
    return 255;
}

// ── Sub-zone hit tests ──

int8_t dashboardHitTestCarousel(int16_t x, int16_t y) {
    // Check X range (same in screen and sprite space)
    if (x < CAROUSEL_X || x >= CAROUSEL_X + CAROUSEL_W) return 0;

    // Convert Y to sprite coordinates
    int sprY = y - Z1_Y;
    int step = CAROUSEL_ITEM_H + CAROUSEL_ITEM_GAP;

    // Selected item bounds in sprite space
    int selTop = CAROUSEL_CY - CAROUSEL_ITEM_H / 2;
    int selBot = CAROUSEL_CY + CAROUSEL_ITEM_H / 2;

    // 2 slots above selected
    if (sprY >= selTop - 2 * step && sprY < selTop - step) return -2;

    // 1 slot above selected
    if (sprY >= selTop - step && sprY < selTop) return -1;

    // 1 slot below selected
    if (sprY >= selBot && sprY < selBot + step) return +1;

    // 2 slots below selected
    if (sprY >= selBot + step && sprY < selBot + 2 * step) return +2;

    return 0;
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

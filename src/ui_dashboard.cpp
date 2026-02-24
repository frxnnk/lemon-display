#include "ui_dashboard.h"
#include "display_manager.h"
#include "ui_components.h"
#include "colors.h"
#include "config.h"
#include "touch_utils.h"
#include "data/lemon_logo.h"
#include "data/market_icons.h"
#include "data/satoshi_fonts.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// ══════════════════════════════════════════
//  LAYOUT v4.0 (480x480, BTC hero + Lemon dollar)
// ══════════════════════════════════════════

#define MARGIN     16
#define GAP         6
#define CARD_PAD   14
#define CARD_W    448   // 480 - 2*MARGIN
#define CARD_R     16

// Chart-specific padding (tighter than CARD_PAD for edge-to-edge feel)
#define CHART_PAD_X  6   // Horizontal: chart ↔ card edge
#define CHART_PAD_B  6   // Bottom: chart ↔ card bottom

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
#define PAIR_LABEL_Y     10
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
static bool    pairSelectorEnabled  = true;

static uint8_t btcVisibleIdx[BTC_PERIOD_COUNT] = {};
static uint8_t btcVisibleCount = 0;
static uint8_t btcSelectedSlot = 0;
static uint8_t dollarVisibleIdx[DOLLAR_PERIOD_COUNT] = {};
static uint8_t dollarVisibleCount = 0;
static uint8_t dollarSelectedSlot = 0;

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

static void drawBitcoinMiniIcon(LGFX_Sprite& spr, int x, int y) {
    drawBtcLogo28(spr, x, y);
}

static void drawDigitalDollarMiniIcon(LGFX_Sprite& spr, int x, int y, bool compact) {
    if (!compact) {
        drawUsdLogo28(spr, x, y);
        return;
    }

    drawUsdLogo20(spr, x, y);
}

static int findSlotForRealIdx(const uint8_t* arr, uint8_t count, uint8_t realIdx) {
    for (uint8_t i = 0; i < count; i++) {
        if (arr[i] == realIdx) return (int)i;
    }
    return -1;
}

static void resetBtcFilterToAll() {
    btcVisibleCount = BTC_PERIOD_COUNT;
    for (uint8_t i = 0; i < BTC_PERIOD_COUNT; i++) btcVisibleIdx[i] = i;
    int slot = findSlotForRealIdx(btcVisibleIdx, btcVisibleCount, 0);
    btcSelectedSlot = (slot >= 0) ? (uint8_t)slot : 0;
}

static void resetDollarFilterToAll() {
    dollarVisibleCount = DOLLAR_PERIOD_COUNT;
    for (uint8_t i = 0; i < DOLLAR_PERIOD_COUNT; i++) dollarVisibleIdx[i] = i;
    int slot = findSlotForRealIdx(dollarVisibleIdx, dollarVisibleCount, 0);
    dollarSelectedSlot = (slot >= 0) ? (uint8_t)slot : 0;
}

// ══════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════

void dashboardSetup() {
    tft.fillScreen(Colors::BG_BASE);
    resetBtcFilterToAll();
    resetDollarFilterToAll();
    pairSelectorEnabled = true;

    // Allocate persistent zone sprites in PSRAM (once, never freed)
    sprZ0.setPsram(true);
    sprZ0.setColorDepth(16);
    sprZ0.createSprite(SCREEN_W, Z0_H);

    sprZ1.setPsram(true);
    sprZ1.setColorDepth(16);
    sprZ1.createSprite(SCREEN_W, z1H);

    if (z2H > 0) {
        sprZ2.setPsram(true);
        sprZ2.setColorDepth(16);
        sprZ2.createSprite(SCREEN_W, z2H);
    }

    spritesReady = true;

    // Fill gaps between zones once (static, never need redrawing)
    tft.fillRect(0, Z0_Y + Z0_H, SCREEN_W, Z1_Y - (Z0_Y + Z0_H), Colors::BG_BASE);
    tft.fillRect(0, Z1_Y + z1H, SCREEN_W, SCREEN_H - (Z1_Y + z1H), Colors::BG_BASE);

    Serial.printf("[Dashboard] Zone sprites allocated: Z0=%dB Z1=%dB Z2=%dB\n",
                  SCREEN_W * Z0_H * 2, SCREEN_W * z1H * 2, SCREEN_W * z2H * 2);
}

// ══════════════════════════════════════════
//  LAYOUT PRESETS
// ══════════════════════════════════════════

uint8_t dashboardGetLayout() {
    return currentLayout;
}

int dashboardGetZ2Y() { return z2Y; }
int dashboardGetZ2H() { return z2H; }

void dashboardSetLayout(uint8_t idx) {
    if (idx > 1) idx = 0;
    currentLayout = idx;

    // Available vertical space below header
    const int avail = SCREEN_H - Z1_Y;  // 432px

    switch (idx) {
        case 0:  // BTC only — Z1 fills all space, no Z2
            z1H = avail; z2H = 0; break;
        default: // BTC + USD (50/50)
            z1H = (avail - GAP) / 2;    // 213
            z2H = avail - z1H - GAP;    // 213
            break;
    }
    z2Y = Z1_Y + z1H + GAP;

    // Recreate sprites at new sizes
    if (spritesReady) {
        sprZ1.deleteSprite();

        sprZ1.setPsram(true);
        sprZ1.setColorDepth(16);
        sprZ1.createSprite(SCREEN_W, z1H);

        if (z2H > 0) {
            sprZ2.deleteSprite();
            sprZ2.setPsram(true);
            sprZ2.setColorDepth(16);
            sprZ2.createSprite(SCREEN_W, z2H);
        } else {
            sprZ2.deleteSprite();
        }

        // Refill gaps (VSync-protected to avoid visible tear)
        displayWaitVSync();
        tft.fillRect(0, Z1_Y + z1H, SCREEN_W, SCREEN_H - (Z1_Y + z1H), Colors::BG_BASE);

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

    // Full imagotipo (122x28 — icon + LEMON wordmark)
    int logoX = MARGIN;
    int logoY = (Z0_H - 28) / 2;
    drawLemonImagotipo122(sprZ0, logoX, logoY);

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

// ── Helper: draw vertical carousel (tap-only, no momentum) ──
static void drawCarousel(LGFX_Sprite& spr, uint8_t selectedPeriod) {
    if (btcVisibleCount == 0) return;

    int cx = CAROUSEL_X + CAROUSEL_W / 2;
    int step = CAROUSEL_ITEM_H + CAROUSEL_ITEM_GAP;
    int selectedSlot = findSlotForRealIdx(btcVisibleIdx, btcVisibleCount, selectedPeriod);
    if (selectedSlot < 0) selectedSlot = 0;
    btcSelectedSlot = (uint8_t)selectedSlot;

    // Selected item background (always at center)
    int selY = CAROUSEL_CY - CAROUSEL_ITEM_H / 2;
    spr.fillSmoothRoundRect(CAROUSEL_X, selY, CAROUSEL_W, CAROUSEL_ITEM_H, 6, Colors::BG_ELEVATED);

    // Green accent dot on left of selected
    spr.fillSmoothCircle(CAROUSEL_X + 8, CAROUSEL_CY, 3, Colors::LEMON_GREEN);

    // Draw selected label
    spr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_ELEVATED);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString(BTC_PERIODS[btcVisibleIdx[selectedSlot]].label, cx + 4, CAROUSEL_CY, &Satoshi12);

    // Draw up to 2 items above and 2 below (skip if outside safe zone)
    const int carouselMinCY = 8;   // Top margin
    const int carouselMaxCY = 96;  // Chart starts at y=100; carousel is on far right, no overlap with center text
    for (int d = 1; d <= 2; d++) {
        int idxAbove = selectedSlot - d;
        if (idxAbove >= 0 && idxAbove < (int)btcVisibleCount) {
            int itemCY = CAROUSEL_CY - d * step;
            if (itemCY >= carouselMinCY && itemCY <= carouselMaxCY) {
                uint16_t color = (d == 1) ? Colors::TEXT_SECONDARY : Colors::TEXT_TERTIARY;
                spr.setTextColor(color, Colors::BG_CARD);
                spr.setTextDatum(lgfx::middle_center);
                spr.drawString(BTC_PERIODS[btcVisibleIdx[idxAbove]].label, cx, itemCY, &Satoshi12);
            }
        }

        int idxBelow = selectedSlot + d;
        if (idxBelow >= 0 && idxBelow < (int)btcVisibleCount) {
            int itemCY = CAROUSEL_CY + d * step;
            if (itemCY >= carouselMinCY && itemCY <= carouselMaxCY) {
                uint16_t color = (d == 1) ? Colors::TEXT_SECONDARY : Colors::TEXT_TERTIARY;
                spr.setTextColor(color, Colors::BG_CARD);
                spr.setTextDatum(lgfx::middle_center);
                spr.drawString(BTC_PERIODS[btcVisibleIdx[idxBelow]].label, cx, itemCY, &Satoshi12);
            }
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

// ── Direct-to-framebuffer time update (stable-width, single clear) ──
void dashboardUpdateTimeDirect(const char* timeStr) {
    if (!spritesReady) return;

    // Measure max digit width for Orbitron (cached)
    static int maxDigitW_clock = 0;
    static int colonW_clock = 0;
    static int spaceW_clock = 0;
    if (maxDigitW_clock == 0) {
        char d[2] = {0, 0};
        for (int i = 0; i <= 9; i++) {
            d[0] = '0' + i;
            int w = tft.textWidth(d, &fonts::Orbitron_Light_24);
            if (w > maxDigitW_clock) maxDigitW_clock = w;
        }
        colonW_clock = tft.textWidth(":", &fonts::Orbitron_Light_24);
        spaceW_clock = tft.textWidth(" ", &fonts::Orbitron_Light_24);
    }

    // Fixed max width: covers both "HH:MM:SS" (24h) and "HH:MM:SS PM" (12h worst case)
    // 8 digits + 2 colons + space + 2 letters is the absolute max
    static int maxTotalW = 0;
    if (maxTotalW == 0) {
        // "12:00:00 PM" = 6 digits + 2 colons + 1 space + "PM"
        int pmW = tft.textWidth("PM", &fonts::Orbitron_Light_24);
        maxTotalW = 6 * maxDigitW_clock + 2 * colonW_clock + spaceW_clock + pmW;
        // Also check plain 24h: "00:00:00" = 6 digits + 2 colons
        int w24h = 6 * maxDigitW_clock + 2 * colonW_clock;
        if (w24h > maxTotalW) maxTotalW = w24h;
    }

    // Calculate actual width of current string
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

    // Always use maxTotalW for positioning — prevents lateral shift
    int timeRightX = SCREEN_W - MARGIN;
    int clearStartX = timeRightX - maxTotalW;
    int startX = timeRightX - totalW;  // right-aligned within fixed area
    int cy_spr = Z0_H / 2;
    int cy_scr = Z0_Y + cy_spr;
    int cellH = 30;
    int cellY_spr = cy_spr - cellH / 2;
    int cellY_scr = cy_scr - cellH / 2;

    // Update sprZ0 for consistency with future full pushes
    sprZ0.fillRect(clearStartX, cellY_spr, maxTotalW, cellH, Colors::BG_BASE);
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

    // Atomic clipped push from sprZ0 (no intermediate blank frame)
    displayWaitVSync();
    tft.setClipRect(clearStartX, Z0_Y + cellY_spr, maxTotalW, cellH);
    sprZ0.pushSprite(0, Z0_Y);
    tft.clearClipRect();
}

// ── Direct-to-framebuffer price update (stable-width, single strip clear) ──
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

    // Price area in sprite coords (below pair label, between carousels)
    const int STRIP_Y = 30;   // logo 20px at Y=10 ends at Y=30 — no overlap
    const int STRIP_H = 50;
    const int STRIP_X = MARGIN + CARD_PAD;
    const int STRIP_W = CAROUSEL_X - STRIP_X - 2;

    // Use smaller font for non-USD pairs
    const lgfx::IFont* priceFont = (selectedPair == 0) ? &SatoshiBold40 : &SatoshiBold24;

    // Update sprZ1 to stay in sync — pad 2px per side for $ glyph overshoot
    sprZ1.fillRect(STRIP_X - 2, STRIP_Y, STRIP_W + 4, STRIP_H, Colors::BG_CARD);
    drawFixedWidthPrice(sprZ1, priceBuf, PRICE_CX, PRICE_CY, priceFont, priceColor, Colors::BG_CARD);

    // Atomic clipped push from sprZ1 (no intermediate blank frame)
    displayWaitVSync();
    tft.setClipRect(STRIP_X - 2, Z1_Y + STRIP_Y, STRIP_W + 4, STRIP_H);
    sprZ1.pushSprite(0, Z1_Y);
    tft.clearClipRect();
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
    bool simpleMode = !pairSelectorEnabled;

    if (!btc.valid) {
        char loadBuf[24];
        snprintf(loadBuf, sizeof(loadBuf), "%s...", simpleMode ? "Bitcoin" : pair.pairLabel);
        drawCentered(sprZ1, loadBuf, z1H / 2 - 10,
                     &SatoshiMedium18, Colors::TEXT_SECONDARY);
        sprZ1.pushSprite(0, Z1_Y);
        dirtyZones |= (1 << 1);
        return;
    }

    // ── Top row: pair label (tappable — opens dropdown) ──
    sprZ1.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
    sprZ1.setTextDatum(lgfx::top_left);
    if (simpleMode) {
        drawBtcLogo20(sprZ1, PAIR_LABEL_X, PAIR_LABEL_Y);
        sprZ1.setTextDatum(lgfx::middle_left);
        sprZ1.drawString("Bitcoin", PAIR_LABEL_X + 24, PAIR_LABEL_Y + 10, &Satoshi12);
        sprZ1.setTextDatum(lgfx::top_left);
    } else {
        sprZ1.setTextDatum(lgfx::middle_left);
        sprZ1.drawString(pair.pairLabel, PAIR_LABEL_X, PAIR_LABEL_Y + 10, &Satoshi12);
        if (pairSelectorEnabled) {
            int chevX = PAIR_LABEL_X + sprZ1.textWidth(pair.pairLabel, &Satoshi12) + 6;
            int chevY = PAIR_LABEL_Y + 7;
            sprZ1.fillTriangle(chevX, chevY, chevX + 8, chevY, chevX + 4, chevY + 5, Colors::TEXT_SECONDARY);
        }
        sprZ1.setTextDatum(lgfx::top_left);
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

    drawFixedWidthPrice(sprZ1, priceBuf, PRICE_CX, PRICE_CY, priceFont, priceColor, Colors::BG_CARD);

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

    // ── Chart area (bottom portion — tight padding for edge-to-edge feel) ──
    int chartX = MARGIN + CHART_PAD_X;
    int chartY = 100;
    int chartW = CARD_W - 2 * CHART_PAD_X;
    int chartH = z1H - chartY - CHART_PAD_B;

    if (chartH > 10) {
        // Clip sparkline drawing so glow dots / wide lines can't leak
        // outside chart bounds (prevents ghost pixels during morph)
        sprZ1.setClipRect(chartX, chartY, chartW, chartH);

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

        sprZ1.clearClipRect();
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
    if (pairDropdownOpen && pairSelectorEnabled) {
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
    drawFixedWidthPrice(sprZ1, priceBuf, PRICE_CX, PRICE_CY, priceFont, priceColor, Colors::BG_CARD);

    sprZ1.clearClipRect();

    // VSync + push only the price strip (much less PSRAM bus contention than full Z1)
    displayWaitVSync();
    tft.setClipRect(CLIP_X, Z1_Y + STRIP_Y, CLIP_W, STRIP_H);
    sprZ1.pushSprite(0, Z1_Y);
    tft.clearClipRect();
}

// ── Chart-only partial update (for morph animation — avoids full 288KB sprite push) ──
void dashboardRedrawChartOnly(const SparklineData& spark, ChartStyle chartStyle, const OhlcData* ohlc, float ath) {
    if (!spritesReady) return;

    const int chartX = MARGIN + CHART_PAD_X;
    const int chartY = 100;
    const int chartW = CARD_W - 2 * CHART_PAD_X;
    const int chartH = z1H - chartY - CHART_PAD_B;
    if (chartH <= 10) return;

    // Clear with 2px margin above/below chart, staying clear of card rounded corners
    const int clearY = chartY - 2;
    const int clearH = chartH + 4;  // chart area + 2px margin each side (avoids card corners)
    sprZ1.fillRect(chartX, clearY, chartW, clearH, Colors::BG_CARD);

    // Clip sprite drawing to the clear rect so glow dots / wide lines
    // can't leak pixels outside (those would appear delayed on next full push)
    sprZ1.setClipRect(chartX, clearY, chartW, clearH);

    bool zoomed = chartZoom.active && chartZoom.zoomLevel > 1.01f;

    switch (chartStyle) {
        case CHART_CANDLE:
            if (ohlc && ohlc->valid && ohlc->count >= 2) {
                drawCandlestick(sprZ1, chartX, chartY, chartW, chartH, *ohlc);
            } else if (spark.valid && spark.count >= 2) {
                if (zoomed)
                    drawSparklineZoomed(sprZ1, chartX, chartY, chartW, chartH,
                                        spark, Colors::CHART_LINE, Colors::CHART_FILL,
                                        chartZoom.zoomLevel, chartZoom.panOffset);
                else
                    drawSparkline(sprZ1, chartX, chartY, chartW, chartH,
                                  spark, Colors::CHART_LINE, Colors::CHART_FILL);
            }
            break;
        case CHART_MARKERS:
            if (spark.valid && spark.count >= 2) {
                if (zoomed)
                    drawSparklineZoomed(sprZ1, chartX, chartY, chartW, chartH,
                                        spark, Colors::CHART_LINE, Colors::CHART_FILL,
                                        chartZoom.zoomLevel, chartZoom.panOffset);
                else
                    drawSparkline(sprZ1, chartX, chartY, chartW, chartH,
                                  spark, Colors::CHART_LINE, Colors::CHART_FILL);
                if (!zoomed) {
                    drawChartMarkers(sprZ1, chartX, chartY, chartW, chartH, spark, ath);
                }
            }
            break;
        case CHART_LINE:
        default:
            if (spark.valid && spark.count >= 2) {
                if (zoomed)
                    drawSparklineZoomed(sprZ1, chartX, chartY, chartW, chartH,
                                        spark, Colors::CHART_LINE, Colors::CHART_FILL,
                                        chartZoom.zoomLevel, chartZoom.panOffset);
                else
                    drawSparkline(sprZ1, chartX, chartY, chartW, chartH,
                                  spark, Colors::CHART_LINE, Colors::CHART_FILL);
            }
            break;
    }

    // Zoom badge
    if (chartZoom.active && chartZoom.zoomLevel > 1.01f) {
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

    sprZ1.clearClipRect();

    // Clipped push — only the chart region (~85KB instead of 288KB)
    displayWaitVSync();
    tft.setClipRect(chartX, Z1_Y + clearY, chartW, clearH);
    sprZ1.pushSprite(0, Z1_Y);
    tft.clearClipRect();
    dirtyZones |= (1 << 1);
}

// ══════════════════════════════════════════
//  Z2: DOLAR DIGITAL (120px)
// ══════════════════════════════════════════

// Dollar period labels come from DOLLAR_PERIODS[] in config.h

#define DCAR_W          84
#define DCAR_ITEM_H     28
#define DCAR_ITEM_GAP    2
#define DCAR_X          (MARGIN + CARD_W - CARD_PAD - DCAR_W)

// Dollar carousel CY — aligned with dollar price text
static int dollarCarouselCY() {
    return (z2H <= 60) ? 30 : 44;  // matches priceY in dashboardDrawLemonDollar (+4 for card top padding)
}

static void drawDollarCarousel(LGFX_Sprite& spr, uint8_t selected) {
    if (dollarVisibleCount == 0) return;

    int cy = dollarCarouselCY();
    int cx = DCAR_X + DCAR_W / 2;
    int step = DCAR_ITEM_H + DCAR_ITEM_GAP;
    int selectedSlot = findSlotForRealIdx(dollarVisibleIdx, dollarVisibleCount, selected);
    if (selectedSlot < 0) selectedSlot = 0;
    dollarSelectedSlot = (uint8_t)selectedSlot;

    int selY = cy - DCAR_ITEM_H / 2;
    spr.fillSmoothRoundRect(DCAR_X, selY, DCAR_W, DCAR_ITEM_H, 6, Colors::BG_ELEVATED);

    spr.fillSmoothCircle(DCAR_X + 8, cy, 3, Colors::NEBULA);

    spr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_ELEVATED);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString(DOLLAR_PERIODS[dollarVisibleIdx[selectedSlot]].label, cx + 4, cy, &Satoshi12);

    // Draw up to 2 items above and 2 below (clamp to safe zone)
    const int dcarMinCY = 12;   // top margin (card starts at y=4)
    const int dcarMaxCY = 79;   // chart starts at y=86, keep labels above
    for (int d = 1; d <= 2; d++) {
        int idxAbove = selectedSlot - d;
        if (idxAbove >= 0 && idxAbove < (int)dollarVisibleCount) {
            int itemCY = cy - d * step;
            if (itemCY >= dcarMinCY && itemCY <= dcarMaxCY) {
                uint16_t color = (d == 1) ? Colors::TEXT_SECONDARY : Colors::TEXT_TERTIARY;
                spr.setTextColor(color, Colors::BG_CARD);
                spr.setTextDatum(lgfx::middle_center);
                spr.drawString(DOLLAR_PERIODS[dollarVisibleIdx[idxAbove]].label, cx, itemCY, &Satoshi12);
            }
        }

        int idxBelow = selectedSlot + d;
        if (idxBelow >= 0 && idxBelow < (int)dollarVisibleCount) {
            int itemCY = cy + d * step;
            if (itemCY >= dcarMinCY && itemCY <= dcarMaxCY) {
                uint16_t color = (d == 1) ? Colors::TEXT_SECONDARY : Colors::TEXT_TERTIARY;
                spr.setTextColor(color, Colors::BG_CARD);
                spr.setTextDatum(lgfx::middle_center);
                spr.drawString(DOLLAR_PERIODS[dollarVisibleIdx[idxBelow]].label, cx, itemCY, &Satoshi12);
            }
        }
    }
}

void dashboardDrawLemonDollar(const LemonPrice& lemon, const SparklineData* lemonSpark,
                              uint8_t dollarPeriod, ChartStyle dollarChartStyle,
                              float dollarChange) {
    if (z2H <= 0) return;  // BTC-only layout — no Z2
    sprZ2.fillSprite(Colors::BG_BASE);
    bool simpleMode = !pairSelectorEnabled;

    // Card top offset — visual gap between Z1 bottom and Z2 card
    bool compact = (z2H <= 60);
    const int ct = compact ? 0 : 4;

    drawGlassCard(sprZ2, MARGIN, ct, CARD_W, z2H - ct, CARD_R);

    if (!lemon.valid) {
        drawCentered(sprZ2, simpleMode ? "Dolar Digital..." : "USDC/ARS...", ct + (z2H - ct) / 2 - 6,
                     &Satoshi12, Colors::TEXT_SECONDARY);
        sprZ2.pushSprite(0, z2Y);
        dirtyZones |= (1 << 2);
        return;
    }

    // ── Adaptive layout based on zone height ──
    int labelY  = compact ? 6  : (6 + ct);
    int priceY  = compact ? 30 : (40 + ct);
    const lgfx::IFont* priceFont = compact ? &SatoshiMedium18 : &SatoshiBold24;

    // ── Pair label top-left ──
    if (simpleMode) {
        int iconSize = compact ? 20 : 28;
        int iconX = MARGIN + CARD_PAD;
        int iconY = compact ? (labelY + 1) : (labelY - 1);
        drawDigitalDollarMiniIcon(sprZ2, iconX, iconY, compact);
        int textX = iconX + iconSize + 6;
        int textCY = iconY + iconSize / 2;
        sprZ2.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
        sprZ2.setTextDatum(lgfx::middle_left);
        sprZ2.drawString("Dolar Digital", textX, textCY, &Satoshi9);
    } else {
        sprZ2.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        sprZ2.setTextDatum(lgfx::top_left);
        sprZ2.drawString("USDC/ARS", MARGIN + CARD_PAD, labelY, compact ? &Satoshi9 : &Satoshi12);
    }

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

    drawFixedWidthPrice(sprZ2, avgBuf, PRICE_CX, priceY, priceFont, Colors::TEXT_PRIMARY, Colors::BG_CARD);

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
        sprZ2.drawString(changeBuf, PRICE_CX, 66 + ct, &Satoshi12);
    }

    // ── Carousel (right side) ──
    drawDollarCarousel(sprZ2, dollarPeriod);

    // ── Sparkline (bottom, full card width — tight padding) ──
    if (!compact && lemonSpark && lemonSpark->valid && lemonSpark->count >= 2) {
        int chartX = MARGIN + CHART_PAD_X;
        int chartY = 82 + ct;  // offset by card top padding
        int chartW = CARD_W - 2 * CHART_PAD_X;
        int chartH = z2H - chartY - CHART_PAD_B;

        if (chartH > 10) {
            sprZ2.setClipRect(chartX, chartY, chartW, chartH);
            drawSparkline(sprZ2, chartX, chartY, chartW, chartH,
                          *lemonSpark, Colors::NEBULA, Colors::NEBULA_FILL);
            if (dollarChartStyle == CHART_MARKERS) {
                drawChartMarkers(sprZ2, chartX, chartY, chartW, chartH,
                                 *lemonSpark, 0.0f, true);  // ARS format, no ATH
            }
            sprZ2.clearClipRect();
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
    return pairSelectorEnabled && pairDropdownOpen;
}

void dashboardOpenPairDropdown(uint8_t currentPair) {
    if (!pairSelectorEnabled) return;
    pairDropdownOpen = true;
    pairDropdownSelected = currentPair;
}

void dashboardClosePairDropdown() {
    pairDropdownOpen = false;
}

int8_t dashboardHitTestPairDropdown(int16_t x, int16_t y) {
    if (!pairSelectorEnabled || !pairDropdownOpen) return -1;

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
    if (!pairSelectorEnabled) return false;
    int sprY = y - Z1_Y;
    return (x >= PAIR_LABEL_X && x < PAIR_LABEL_X + PAIR_LABEL_W &&
            sprY >= PAIR_LABEL_Y && sprY < PAIR_LABEL_Y + PAIR_LABEL_H);
}

void dashboardSetPairSelectorEnabled(bool enabled) {
    pairSelectorEnabled = enabled;
    if (!enabled) {
        pairDropdownOpen = false;
    }
}

void dashboardSetBtcCarouselFilter(const uint8_t* idxList, uint8_t count, uint8_t selectedRealIdx) {
    if (!idxList || count == 0 || count > BTC_PERIOD_COUNT) {
        resetBtcFilterToAll();
    } else {
        btcVisibleCount = count;
        for (uint8_t i = 0; i < count; i++) {
            btcVisibleIdx[i] = idxList[i];
        }
    }

    int slot = findSlotForRealIdx(btcVisibleIdx, btcVisibleCount, selectedRealIdx);
    if (slot < 0) slot = 0;
    btcSelectedSlot = (uint8_t)slot;
    btcCarousel.scrollOffset = (float)btcSelectedSlot;
    btcCarousel.velocity = 0.0f;
    btcCarousel.animating = false;
}

void dashboardSetDollarCarouselFilter(const uint8_t* idxList, uint8_t count, uint8_t selectedRealIdx) {
    if (!idxList || count == 0 || count > DOLLAR_PERIOD_COUNT) {
        resetDollarFilterToAll();
    } else {
        dollarVisibleCount = count;
        for (uint8_t i = 0; i < count; i++) {
            dollarVisibleIdx[i] = idxList[i];
        }
    }

    int slot = findSlotForRealIdx(dollarVisibleIdx, dollarVisibleCount, selectedRealIdx);
    if (slot < 0) slot = 0;
    dollarSelectedSlot = (uint8_t)slot;
    dollarCarouselState.scrollOffset = (float)dollarSelectedSlot;
    dollarCarouselState.velocity = 0.0f;
    dollarCarouselState.animating = false;
}

// ══════════════════════════════════════════
//  FULL REDRAW
// ══════════════════════════════════════════

void dashboardDrawAll(const char* timeStr,
                      const BtcPrice& btc, const SparklineData& spark,
                      uint8_t selectedPeriod,
                      uint8_t selectedPair,
                      const LemonPrice& lemon,
                      bool offline, bool wsConnected,
                      const float* periodChanges,
                      ChartStyle chartStyle, const OhlcData* ohlc,
                      const SparklineData* lemonSpark,
                      uint8_t dollarPeriod,
                      ChartStyle dollarChartStyle,
                      float dollarChange) {
    dashboardDrawHeader(timeStr, offline, wsConnected);
    dashboardDrawBtcHero(btc, spark, selectedPeriod, periodChanges, chartStyle, ohlc, selectedPair);
    if (z2H > 0) {
        dashboardDrawLemonDollar(lemon, lemonSpark, dollarPeriod, dollarChartStyle, dollarChange);
    }
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
    if ((dz & (1 << 2)) && z2H > 0) {
        sprZ2.pushSprite(0, z2Y);
    }
}

void dashboardPushSpotlight(int16_t sx, int16_t sy, int16_t sw, int16_t sh) {
    if (!spritesReady) return;
    displayWaitVSync();
    tft.setClipRect(sx, sy, sw, sh);
    sprZ0.pushSprite(0, Z0_Y);
    sprZ1.pushSprite(0, Z1_Y);
    if (z2H > 0) sprZ2.pushSprite(0, z2Y);
    tft.clearClipRect();
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
    // Everything below Z1 (gap + Z2 area + below Z2)
    tft.fillRect(0, Z1_Y + z1H, SCREEN_W, SCREEN_H - (Z1_Y + z1H), Colors::BG_BASE);
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

// ══════════════════════════════════════════
//  PREDICTION MODE (Polymarket) — renders in Z2
// ══════════════════════════════════════════

#include "data_models.h"

static bool predictionModeActive = false;

// Button geometry (in Z2 sprite coords) — adapted for 240px height
#define PRED_BAR_Y    68
#define PRED_BAR_H    18
#define PRED_BTN_Y    96
#define PRED_BTN_H    46
#define PRED_BTN_GAP  16
#define PRED_BTN_W    ((CARD_W - 2 * CARD_PAD - PRED_BTN_GAP) / 2)
#define PRED_BTN_YES_X (MARGIN + CARD_PAD)
#define PRED_BTN_NO_X  (PRED_BTN_YES_X + PRED_BTN_W + PRED_BTN_GAP)

static bool isLeapYear(int y) {
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static int daysInMonth(int y, int m) {
    static const int dpm[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2) return isLeapYear(y) ? 29 : 28;
    if (m < 1 || m > 12) return 30;
    return dpm[m - 1];
}

static bool formatEndDateAR(const char* isoUtc, char* out, size_t outSize) {
    if (!isoUtc || !out || outSize < 8) return false;

    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if (sscanf(isoUtc, "%4d-%2d-%2dT%2d:%2d", &y, &mo, &d, &h, &mi) != 5) {
        return false;
    }

    // UTC -> Argentina (UTC-3).
    int totalMin = h * 60 + mi;
    totalMin += (-3 * 60);

    while (totalMin < 0) {
        totalMin += 24 * 60;
        d -= 1;
        if (d <= 0) {
            mo -= 1;
            if (mo <= 0) {
                mo = 12;
                y -= 1;
            }
            d = daysInMonth(y, mo);
        }
    }

    while (totalMin >= 24 * 60) {
        totalMin -= 24 * 60;
        d += 1;
        int dim = daysInMonth(y, mo);
        if (d > dim) {
            d = 1;
            mo += 1;
            if (mo > 12) {
                mo = 1;
                y += 1;
            }
        }
    }

    h = totalMin / 60;
    mi = totalMin % 60;

    snprintf(out, outSize, "%02d/%02d %02d:%02d", d, mo, h, mi);
    return true;
}

static void buildPredictionQuestionAR(const PolyMarket& mkt, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';

    // Polymarket short BTC markets include ET times in question text.
    // Replace with AR-local close time to avoid timezone confusion.
    if (strstr(mkt.question, "Bitcoin Up or Down") && strstr(mkt.question, " ET")) {
        char arBuf[24];
        if (formatEndDateAR(mkt.endDate, arBuf, sizeof(arBuf))) {
            const char* hhmm = strchr(arBuf, ' ');
            hhmm = hhmm ? (hhmm + 1) : arBuf;
            snprintf(out, outSize, "Bitcoin sube o baja para las %s AR?", hhmm);
            return;
        }
    }

    strncpy(out, mkt.question, outSize - 1);
    out[outSize - 1] = '\0';
}

bool dashboardIsPredictionMode() {
    return predictionModeActive;
}

void dashboardSetPredictionMode(bool active) {
    predictionModeActive = active;
}

void dashboardSetPredictionLayout(bool active) {
    if (active) {
        // Z1 shrinks to mini BTC hero, Z2 expands for prediction UI
        z1H = 220;
        z2H = 206;
    } else {
        // Restore from current layout preset
        const int avail = SCREEN_H - Z1_Y;
        switch (currentLayout) {
            case 0:  z1H = avail; z2H = 0; break;  // BTC only
            default: z1H = (avail - GAP) / 2; z2H = avail - z1H - GAP; break;  // 50/50
        }
    }
    z2Y = Z1_Y + z1H + GAP;

    // Recreate sprites at new sizes
    if (spritesReady) {
        sprZ1.deleteSprite();

        sprZ1.setPsram(true);
        sprZ1.setColorDepth(16);
        sprZ1.createSprite(SCREEN_W, z1H);

        if (z2H > 0) {
            sprZ2.deleteSprite();
            sprZ2.setPsram(true);
            sprZ2.setColorDepth(16);
            sprZ2.createSprite(SCREEN_W, z2H);
        } else {
            sprZ2.deleteSprite();
        }

        // Refill gaps
        dashboardFillGaps();

        Serial.printf("[Dashboard] Prediction layout: Z1=%d Z2=%d z2Y=%d\n", z1H, z2H, z2Y);
    }
}

void dashboardDrawPrediction(const PolyMarket* markets, uint8_t count, uint8_t selected,
                             const PolyPrediction* activePred, const PolyStats& stats,
                             bool loading, const char* statusMsg, float refPriceUsd) {
    if (z2H <= 0) return;  // BTC-only layout — no Z2
    sprZ2.fillSprite(Colors::BG_BASE);
    drawGlassCard(sprZ2, MARGIN, 0, CARD_W, z2H, CARD_R);

    // ── Header: page indicator (top-right) ──
    if (count > 0) {
        char pageBuf[8];
        snprintf(pageBuf, sizeof(pageBuf), "%d/%d", selected + 1, count);
        sprZ2.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        sprZ2.setTextDatum(lgfx::top_right);
        sprZ2.drawString(pageBuf, MARGIN + CARD_W - CARD_PAD, 6, &Satoshi9);
    }

    // ── Loading state ──
    if (loading || count == 0) {
        if (loading) {
            drawCentered(sprZ2, "Cargando mercados...", z2H / 2 - 10, &Satoshi12, Colors::TEXT_SECONDARY);
        } else {
            drawCentered(sprZ2, "Por ahora no hay", z2H / 2 - 18, &Satoshi12, Colors::TEXT_SECONDARY);
            drawCentered(sprZ2, "Cambia de temporalidad para ver otra", z2H / 2 + 2, &Satoshi9, Colors::TEXT_TERTIARY);
        }
        displayWaitVSync();
        sprZ2.pushSprite(0, z2Y);
        dirtyZones |= (1 << 2);
        return;
    }

    const PolyMarket& mkt = markets[selected];

    // ── Threshold price (prominent, centered) ──
    {
        if (mkt.refPriceValid && mkt.refPrice > 0.0f) {
            // Exact Chainlink reference price
            char priceBuf[24];
            formatBtcPrice(priceBuf, sizeof(priceBuf), mkt.refPrice);
            sprZ2.setTextColor(Colors::SOLAR, Colors::BG_CARD);
            sprZ2.setTextDatum(lgfx::middle_center);
            sprZ2.drawString(priceBuf, SCREEN_W / 2, 18, &SatoshiMedium18);
        } else {
            sprZ2.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
            sprZ2.setTextDatum(lgfx::middle_center);
            sprZ2.drawString("Cargando umbral...", SCREEN_W / 2, 18, &Satoshi12);
        }
    }

    // ── Question (below threshold, max 2 lines) ──
    char questionBuf[PM_QUESTION_LEN];
    buildPredictionQuestionAR(mkt, questionBuf, sizeof(questionBuf));
    drawWrappedText(sprZ2, questionBuf, MARGIN + CARD_PAD, 30,
                    CARD_W - 2 * CARD_PAD, 16, &Satoshi12, Colors::TEXT_PRIMARY, 2, true);

    // ── Probability bar (Y=64, H=22) ──
    drawProbabilityBar(sprZ2, MARGIN + CARD_PAD, PRED_BAR_Y,
                       CARD_W - 2 * CARD_PAD, PRED_BAR_H, mkt.yesPrice);

    bool hasActive = (activePred && activePred->conditionId[0] != '\0' &&
                      strcmp(activePred->conditionId, mkt.conditionId) == 0);
    drawPredictionButton(sprZ2, PRED_BTN_YES_X, PRED_BTN_Y, PRED_BTN_W, PRED_BTN_H,
                         "SUBE", mkt.yesPrice, true, hasActive);
    drawPredictionButton(sprZ2, PRED_BTN_NO_X, PRED_BTN_Y, PRED_BTN_W, PRED_BTN_H,
                         "BAJA", mkt.noPrice, false, hasActive);

    // ── Stats row in glass card (Y=150, H=32) ──
    {
        int sy = 146;
        int sh = 28;
        drawGlassCard(sprZ2, MARGIN + CARD_PAD, sy, CARD_W - 2 * CARD_PAD, sh, 8);

        char statsBuf[64];
        snprintf(statsBuf, sizeof(statsBuf), "W %d   L %d   Racha %d",
                 stats.wins, stats.losses, stats.streak);
        sprZ2.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        sprZ2.setTextDatum(lgfx::middle_center);
        sprZ2.drawString(statsBuf, SCREEN_W / 2, sy + sh / 2, &Satoshi9);
    }

    // ── Active prediction / last resolution status ──
    if (hasActive || (statusMsg && statusMsg[0])) {
        char predBuf[96];
        bool pendingStatus = false;
        if (hasActive && activePred->resolved == 0) {
            snprintf(predBuf, sizeof(predBuf), "Pendiente: %s (%.0f%%)",
                     activePred->chosenYes ? "SUBE" : "BAJA", activePred->probAtBet * 100);
            sprZ2.setTextColor(Colors::SOLAR, Colors::BG_CARD);
            pendingStatus = true;
        } else if (hasActive && activePred->resolved == 1) {
            snprintf(predBuf, sizeof(predBuf), "Ganaste!");
            sprZ2.setTextColor(Colors::POSITIVE, Colors::BG_CARD);
        } else if (hasActive && activePred->resolved == 2) {
            snprintf(predBuf, sizeof(predBuf), "Perdiste");
            sprZ2.setTextColor(Colors::NEGATIVE, Colors::BG_CARD);
        } else {
            strncpy(predBuf, statusMsg, sizeof(predBuf) - 1);
            predBuf[sizeof(predBuf) - 1] = '\0';
            if (strncmp(predBuf, "Ganaste", 7) == 0) {
                sprZ2.setTextColor(Colors::POSITIVE, Colors::BG_CARD);
            } else if (strncmp(predBuf, "Perdiste", 8) == 0) {
                sprZ2.setTextColor(Colors::NEGATIVE, Colors::BG_CARD);
            } else {
                sprZ2.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
            }
        }
        if (pendingStatus) {
            sprZ2.setTextDatum(lgfx::middle_left);
            sprZ2.drawString(predBuf, MARGIN + CARD_PAD, 181, &Satoshi9);
        } else {
            sprZ2.setTextDatum(lgfx::middle_center);
            sprZ2.drawString(predBuf, SCREEN_W / 2, 181, &Satoshi12);
        }
    }

    // ── Volume + end date (Y=216) ──
    {
        char volBuf[64];

        if (mkt.volume24hr >= 1000000.0f) {
            snprintf(volBuf, sizeof(volBuf), "Vol 24h: $%.1fM", mkt.volume24hr / 1000000.0f);
        } else if (mkt.volume24hr >= 1000.0f) {
            snprintf(volBuf, sizeof(volBuf), "Vol 24h: $%.0fK", mkt.volume24hr / 1000.0f);
        } else {
            snprintf(volBuf, sizeof(volBuf), "Vol 24h: $%.0f", mkt.volume24hr);
        }
        if (mkt.endDate[0]) {
            char arBuf[24];
            char fullBuf[80];
            if (formatEndDateAR(mkt.endDate, arBuf, sizeof(arBuf))) {
                snprintf(fullBuf, sizeof(fullBuf), "Cierra AR: %s", arBuf);
            } else {
                snprintf(fullBuf, sizeof(fullBuf), "Cierra: %s", mkt.endDate);
            }
            sprZ2.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
            sprZ2.setTextDatum(lgfx::middle_right);
            sprZ2.drawString(fullBuf, MARGIN + CARD_W - CARD_PAD, 196, &Satoshi9);
        } else {
            sprZ2.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
            sprZ2.setTextDatum(lgfx::middle_right);
            sprZ2.drawString(volBuf, MARGIN + CARD_W - CARD_PAD, 196, &Satoshi9);
        }
    }
    displayWaitVSync();
    sprZ2.pushSprite(0, z2Y);
    dirtyZones |= (1 << 2);
}

bool dashboardHitTestPredYes(int16_t x, int16_t y) {
    int sprY = y - z2Y;
    return (x >= PRED_BTN_YES_X && x < PRED_BTN_YES_X + PRED_BTN_W &&
            sprY >= PRED_BTN_Y && sprY < PRED_BTN_Y + PRED_BTN_H);
}

bool dashboardHitTestPredNo(int16_t x, int16_t y) {
    int sprY = y - z2Y;
    return (x >= PRED_BTN_NO_X && x < PRED_BTN_NO_X + PRED_BTN_W &&
            sprY >= PRED_BTN_Y && sprY < PRED_BTN_Y + PRED_BTN_H);
}



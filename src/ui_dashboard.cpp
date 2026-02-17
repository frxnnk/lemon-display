#include "ui_dashboard.h"
#include "display_manager.h"
#include "ui_components.h"
#include "colors.h"
#include "config.h"
#include "touch_utils.h"
#include "data/lemon_logo.h"
#include "data/satoshi_fonts.h"
#include <cmath>

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

// ── Carousel layout (inside Z1, right of price) ──
#define CAROUSEL_W        80
#define CAROUSEL_ITEM_H   28
#define CAROUSEL_ITEM_GAP  2
#define CAROUSEL_X        (MARGIN + CARD_W - CARD_PAD - CAROUSEL_W)  // 370
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
        case 1: y = Z1_Y; h = Z1_H; return true;
        case 2: y = Z2_Y; h = Z2_H; return true;
        default: return false;
    }
}

static const char* periodLabels[] = { "15m", "1h", "24h", "7d", "30d", "1Y" };
static const int PERIOD_COUNT = 6;

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
    sprZ1.createSprite(SCREEN_W, Z1_H);

    sprZ2.setPsram(true);
    sprZ2.setColorDepth(16);
    sprZ2.createSprite(SCREEN_W, Z2_H);

    spritesReady = true;

    // Fill gaps between zones once (static, never need redrawing)
    tft.fillRect(0, Z0_Y + Z0_H, SCREEN_W, Z1_Y - (Z0_Y + Z0_H), Colors::BG_BASE);
    tft.fillRect(0, Z1_Y + Z1_H, SCREEN_W, Z2_Y - (Z1_Y + Z1_H), Colors::BG_BASE);
    tft.fillRect(0, Z2_Y + Z2_H, SCREEN_W, SCREEN_H - (Z2_Y + Z2_H), Colors::BG_BASE);

    Serial.printf("[Dashboard] Zone sprites allocated: Z0=%dB Z1=%dB Z2=%dB\n",
                  SCREEN_W * Z0_H * 2, SCREEN_W * Z1_H * 2, SCREEN_W * Z2_H * 2);
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

// ── Helper: draw vertical carousel (3 visible items) ──
static void drawCarousel(LGFX_Sprite& spr, uint8_t selectedPeriod) {
    int cx = CAROUSEL_X + CAROUSEL_W / 2;

    // Selected item background
    int selY = CAROUSEL_CY - CAROUSEL_ITEM_H / 2;
    spr.fillSmoothRoundRect(CAROUSEL_X, selY, CAROUSEL_W, CAROUSEL_ITEM_H, 6, Colors::BG_ELEVATED);

    // Divider lines (iOS picker band)
    spr.drawFastHLine(CAROUSEL_X, selY - 1, CAROUSEL_W, Colors::DIVIDER);
    spr.drawFastHLine(CAROUSEL_X, selY + CAROUSEL_ITEM_H, CAROUSEL_W, Colors::DIVIDER);

    // Green accent dot on left of selected
    spr.fillSmoothCircle(CAROUSEL_X + 8, CAROUSEL_CY, 3, Colors::LEMON_GREEN);

    // Selected label
    spr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_ELEVATED);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString(periodLabels[selectedPeriod], cx + 4, CAROUSEL_CY, &Satoshi12);

    // Previous item (above)
    if (selectedPeriod > 0) {
        int prevCY = CAROUSEL_CY - CAROUSEL_ITEM_H - CAROUSEL_ITEM_GAP;
        spr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        spr.setTextDatum(lgfx::middle_center);
        spr.drawString(periodLabels[selectedPeriod - 1], cx, prevCY, &Satoshi12);
    }

    // Next item (below)
    if (selectedPeriod < PERIOD_COUNT - 1) {
        int nextCY = CAROUSEL_CY + CAROUSEL_ITEM_H + CAROUSEL_ITEM_GAP;
        spr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        spr.setTextDatum(lgfx::middle_center);
        spr.drawString(periodLabels[selectedPeriod + 1], cx, nextCY, &Satoshi12);
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
void dashboardUpdatePriceDirect(const BtcPrice& btc) {
    if (!spritesReady || !btc.valid) return;

    char priceBuf[20];
    formatBtcPrice(priceBuf, sizeof(priceBuf), btc.usd);

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

    // Price area in sprite coords
    const int STRIP_Y = 26;
    const int STRIP_H = 48;
    const int STRIP_X = MARGIN + 1;
    const int STRIP_W = CAROUSEL_X - MARGIN - 2;

    // Update sprZ1 to stay in sync
    sprZ1.fillRect(STRIP_X, STRIP_Y, STRIP_W, STRIP_H, Colors::BG_CARD);
    drawFixedWidthPrice(sprZ1, priceBuf, PRICE_CX, PRICE_CY, &SatoshiBold40, priceColor);

    // Write directly to framebuffer (small area, fits in VBlank)
    displayWaitVSync();
    tft.fillRect(STRIP_X, Z1_Y + STRIP_Y, STRIP_W, STRIP_H, Colors::BG_CARD);
    drawFixedWidthPrice(tft, priceBuf, PRICE_CX, Z1_Y + PRICE_CY, &SatoshiBold40, priceColor);
}

// ══════════════════════════════════════════
//  Z1: BTC HERO + CHART (300px)
// ══════════════════════════════════════════

void dashboardDrawBtcHero(const BtcPrice& btc, const SparklineData& spark, uint8_t selectedPeriod,
                          const float* periodChanges, ChartStyle chartStyle, const OhlcData* ohlc) {
    sprZ1.fillSprite(Colors::BG_BASE);

    // Hero card (accent border, radius 20)
    drawHeroCard(sprZ1, MARGIN, 0, CARD_W, Z1_H);

    if (!btc.valid) {
        drawCentered(sprZ1, "BTC/USD...", Z1_H / 2 - 10,
                     &SatoshiMedium18, Colors::TEXT_SECONDARY);
        sprZ1.pushSprite(0, Z1_Y);
        dirtyZones |= (1 << 1);
        return;
    }

    // ── Top row: "BTC/USD" label ──
    sprZ1.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
    sprZ1.setTextDatum(lgfx::top_left);
    sprZ1.drawString("BTC/USD", MARGIN + CARD_PAD, 10, &Satoshi12);

    // ── Main BTC price (no glow, centered) ──
    char priceBuf[20];
    formatBtcPrice(priceBuf, sizeof(priceBuf), btc.usd);

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

    drawFixedWidthPrice(sprZ1, priceBuf, PRICE_CX, PRICE_CY, &SatoshiBold40, priceColor);

    // ── Selected period change text below price ──
    if (periodChanges) {
        float change = periodChanges[selectedPeriod];
        if (!isnan(change)) {
            bool pos = change >= 0;
            char changeBuf[24];
            snprintf(changeBuf, sizeof(changeBuf), "%s%.1f%% %s",
                     pos ? "+" : "", change, periodLabels[selectedPeriod]);
            uint16_t changeColor = pos ? Colors::POSITIVE : Colors::NEGATIVE;
            sprZ1.setTextColor(changeColor);
            sprZ1.setTextDatum(lgfx::middle_center);
            sprZ1.drawString(changeBuf, PRICE_CX, 80, &Satoshi12);
        }
    }

    // ── Vertical carousel (right side) ──
    drawCarousel(sprZ1, selectedPeriod);

    // ── Chart area (bottom portion) ──
    int chartX = MARGIN + CARD_PAD;
    int chartY = 100;
    int chartW = CARD_W - 2 * CARD_PAD;
    int chartH = Z1_H - chartY - 10;  // 190px

    if (chartH > 10) {
        switch (chartStyle) {
            case CHART_CANDLE:
                if (ohlc && ohlc->valid && ohlc->count >= 2) {
                    drawCandlestick(sprZ1, chartX, chartY, chartW, chartH, *ohlc);
                } else if (spark.valid && spark.count >= 2) {
                    // Fallback to line if no OHLC data
                    drawSparkline(sprZ1, chartX, chartY, chartW, chartH,
                                  spark, Colors::CHART_LINE, Colors::CHART_FILL);
                }
                break;

            case CHART_MARKERS:
                if (spark.valid && spark.count >= 2) {
                    drawSparkline(sprZ1, chartX, chartY, chartW, chartH,
                                  spark, Colors::CHART_LINE, Colors::CHART_FILL);
                    drawChartMarkers(sprZ1, chartX, chartY, chartW, chartH,
                                    spark, btc.ath);
                }
                break;

            case CHART_LINE:
            default:
                if (spark.valid && spark.count >= 2) {
                    drawSparkline(sprZ1, chartX, chartY, chartW, chartH,
                                  spark, Colors::CHART_LINE, Colors::CHART_FILL);
                }
                break;
        }

    }

    // Flash border (drawn last, on top of everything)
    drawFlashBorderIfActive(sprZ1, 1, Z1_H);

    displayWaitVSync();
    sprZ1.pushSprite(0, Z1_Y);
    dirtyZones |= (1 << 1);
}

// ── Price-only partial update ──
// Only redraws the price text strip, clipped left of carousel to preserve it.
void dashboardDrawPriceOnly(const BtcPrice& btc) {
    if (!spritesReady || !btc.valid) return;

    // Generous strip covering full SatoshiBold40 text height around PRICE_CY=50
    const int STRIP_Y = 24;
    const int STRIP_H = 56;   // covers y=24..80, well beyond font extents
    const int CLIP_W  = CAROUSEL_X - 4;  // Stop before carousel area

    // Clip sprite drawing to price strip, left of carousel
    sprZ1.setClipRect(0, STRIP_Y, CLIP_W, STRIP_H);

    // Clear: BG_BASE outside card, BG_CARD inside, accent border
    sprZ1.fillRect(0, STRIP_Y, CLIP_W, STRIP_H, Colors::BG_BASE);
    sprZ1.fillRect(MARGIN + 1, STRIP_Y, CLIP_W - MARGIN - 1, STRIP_H, Colors::BG_CARD);
    sprZ1.drawFastVLine(MARGIN, STRIP_Y, STRIP_H, Colors::CARD_BORDER_ACCENT);

    char priceBuf[20];
    formatBtcPrice(priceBuf, sizeof(priceBuf), btc.usd);

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

    drawFixedWidthPrice(sprZ1, priceBuf, PRICE_CX, PRICE_CY, &SatoshiBold40, priceColor);

    sprZ1.clearClipRect();

    // VSync + push only the price strip (much less PSRAM bus contention than full Z1)
    displayWaitVSync();
    tft.setClipRect(0, Z1_Y + STRIP_Y, CLIP_W, STRIP_H);
    sprZ1.pushSprite(0, Z1_Y);
    tft.clearClipRect();
}

// ══════════════════════════════════════════
//  Z2: DOLAR DIGITAL (120px)
// ══════════════════════════════════════════

// Dollar period carousel (same style as BTC)
static const char* dollarPeriodLabels[] = { "24h", "7d", "30d", "1Y" };
static const int DOLLAR_PERIOD_COUNT = 4;

#define DCAR_W          70
#define DCAR_ITEM_H     24
#define DCAR_ITEM_GAP    2
#define DCAR_X          (MARGIN + CARD_W - CARD_PAD - DCAR_W)
#define DCAR_CY         38

static void drawDollarCarousel(LGFX_Sprite& spr, uint8_t selected) {
    int cx = DCAR_X + DCAR_W / 2;

    int selY = DCAR_CY - DCAR_ITEM_H / 2;
    spr.fillSmoothRoundRect(DCAR_X, selY, DCAR_W, DCAR_ITEM_H, 6, Colors::BG_ELEVATED);

    spr.drawFastHLine(DCAR_X, selY - 1, DCAR_W, Colors::DIVIDER);
    spr.drawFastHLine(DCAR_X, selY + DCAR_ITEM_H, DCAR_W, Colors::DIVIDER);

    spr.fillSmoothCircle(DCAR_X + 8, DCAR_CY, 3, Colors::NEBULA);

    spr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_ELEVATED);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString(dollarPeriodLabels[selected], cx + 4, DCAR_CY, &Satoshi12);

    if (selected > 0) {
        int prevCY = DCAR_CY - DCAR_ITEM_H - DCAR_ITEM_GAP;
        spr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        spr.setTextDatum(lgfx::middle_center);
        spr.drawString(dollarPeriodLabels[selected - 1], cx, prevCY, &Satoshi12);
    }

    if (selected < DOLLAR_PERIOD_COUNT - 1) {
        int nextCY = DCAR_CY + DCAR_ITEM_H + DCAR_ITEM_GAP;
        spr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        spr.setTextDatum(lgfx::middle_center);
        spr.drawString(dollarPeriodLabels[selected + 1], cx, nextCY, &Satoshi12);
    }
}

void dashboardDrawLemonDollar(const LemonPrice& lemon, const SparklineData* lemonSpark,
                              uint8_t dollarPeriod, ChartStyle dollarChartStyle,
                              float dollarChange) {
    sprZ2.fillSprite(Colors::BG_BASE);

    drawGlassCard(sprZ2, MARGIN, 0, CARD_W, Z2_H, CARD_R);

    if (!lemon.valid) {
        drawCentered(sprZ2, "USDT/ARS...", Z2_H / 2 - 6,
                     &Satoshi12, Colors::TEXT_SECONDARY);
        sprZ2.pushSprite(0, Z2_Y);
        dirtyZones |= (1 << 2);
        return;
    }

    // ── "USDT/ARS" top-left ──
    sprZ2.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
    sprZ2.setTextDatum(lgfx::top_left);
    sprZ2.drawString("USDT/ARS", MARGIN + CARD_PAD, 10, &Satoshi12);

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
    sprZ2.drawString(avgBuf, PRICE_CX, 40, &SatoshiBold24);

    if (!isnan(dollarChange) && fabsf(dollarChange) < 1000.0f) {
        char changeBuf[24];
        if (fabsf(dollarChange) >= 100.0f) {
            snprintf(changeBuf, sizeof(changeBuf), "%s%.0f%% %s",
                     dollarChange >= 0 ? "+" : "", dollarChange,
                     dollarPeriodLabels[dollarPeriod]);
        } else {
            snprintf(changeBuf, sizeof(changeBuf), "%s%.1f%% %s",
                     dollarChange >= 0 ? "+" : "", dollarChange,
                     dollarPeriodLabels[dollarPeriod]);
        }
        uint16_t changeColor = dollarChange >= 0 ? Colors::POSITIVE : Colors::NEGATIVE;
        sprZ2.setTextColor(changeColor, Colors::BG_CARD);
        sprZ2.setTextDatum(lgfx::middle_center);
        sprZ2.drawString(changeBuf, PRICE_CX, 60, &Satoshi12);
    }

    // ── Carousel (right side) ──
    drawDollarCarousel(sprZ2, dollarPeriod);

    // ── Sparkline (bottom) ──
    if (lemonSpark && lemonSpark->valid && lemonSpark->count >= 2) {
        int chartX = MARGIN + CARD_PAD;
        int chartY = 76;
        int chartW = CARD_W - 2 * CARD_PAD;
        int chartH = Z2_H - chartY - 6;

        drawSparkline(sprZ2, chartX, chartY, chartW, chartH,
                      *lemonSpark, Colors::NEBULA, Colors::BG_CARD);
        if (dollarChartStyle == CHART_MARKERS) {
            drawChartMarkers(sprZ2, chartX, chartY, chartW, chartH,
                             *lemonSpark, 0.0f);  // no ATH for ARS
        }
    }

    // Flash border (drawn last, on top of everything)
    drawFlashBorderIfActive(sprZ2, 2, Z2_H);

    sprZ2.pushSprite(0, Z2_Y);
    dirtyZones |= (1 << 2);
}

int8_t dashboardHitTestDollarCarousel(int16_t x, int16_t y) {
    if (x < DCAR_X || x >= DCAR_X + DCAR_W) return 0;

    int sprY = y - Z2_Y;
    int selTop = DCAR_CY - DCAR_ITEM_H / 2;
    int selBot = DCAR_CY + DCAR_ITEM_H / 2;

    int upperTop = selTop - DCAR_ITEM_GAP - DCAR_ITEM_H;
    if (sprY >= upperTop && sprY < selTop) return -1;

    int lowerBot = selBot + DCAR_ITEM_GAP + DCAR_ITEM_H;
    if (sprY >= selBot && sprY < lowerBot) return +1;

    return 0;
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
        sprZ2.pushSprite(0, Z2_Y);
    }
}

void dashboardMarkDirty(uint8_t zoneId) {
    if (zoneId < 3) dirtyZones |= (1 << zoneId);
}

void dashboardMarkAllDirty() {
    dirtyZones = 0x07;
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

int8_t dashboardHitTestCarousel(int16_t x, int16_t y) {
    // Check X range (same in screen and sprite space)
    if (x < CAROUSEL_X || x >= CAROUSEL_X + CAROUSEL_W) return 0;

    // Convert Y to sprite coordinates
    int sprY = y - Z1_Y;

    // Selected item bounds in sprite space
    int selTop = CAROUSEL_CY - CAROUSEL_ITEM_H / 2;  // 47
    int selBot = CAROUSEL_CY + CAROUSEL_ITEM_H / 2;  // 69

    // Upper slot (previous period)
    int upperTop = selTop - CAROUSEL_ITEM_GAP - CAROUSEL_ITEM_H;  // 23
    if (sprY >= upperTop && sprY < selTop) return -1;

    // Lower slot (next period)
    int lowerBot = selBot + CAROUSEL_ITEM_GAP + CAROUSEL_ITEM_H;  // 93
    if (sprY >= selBot && sprY < lowerBot) return +1;

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

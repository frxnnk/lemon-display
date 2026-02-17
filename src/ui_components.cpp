#include "ui_components.h"
#include "display_manager.h"
#include "colors.h"
#include "config.h"
#include "data/satoshi_fonts.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// ── Gradient helper: interpolate RGB565 ──
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

// ── Coin accent color lookup ──
static uint16_t coinAccentColor(CoinId coin) {
    switch (coin) {
        case COIN_BTC:  return Colors::COIN_BTC;
        case COIN_ETH:  return Colors::COIN_ETH;
        case COIN_SOL:  return Colors::COIN_SOL;
        case COIN_USDT: return Colors::COIN_USDT;
        case COIN_USDC: return Colors::COIN_USDC;
        default:        return Colors::TEXT_SECONDARY;
    }
}

// ══════════════════════════════════════════
//  GLASS CARD (standard card style v2)
// ══════════════════════════════════════════

void drawGlassCard(LGFX_Sprite& spr, int x, int y, int w, int h, int radius) {
    // 1px border
    spr.fillSmoothRoundRect(x, y, w, h, radius, Colors::CARD_BORDER);
    // Interior fill (green-tinted dark)
    spr.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, radius - 1, Colors::BG_CARD);
    // Top highlight line (subtle reflection)
    int hlX = x + radius;
    int hlW = w - 2 * radius;
    if (hlW > 0) {
        spr.drawFastHLine(hlX, y + 1, hlW, Colors::CARD_HIGHLIGHT);
    }
}

// ══════════════════════════════════════════
//  HERO CARD (accent border, larger radius)
// ══════════════════════════════════════════

void drawHeroCard(LGFX_Sprite& spr, int x, int y, int w, int h) {
    int radius = 20;
    // Accent border (green tinted)
    spr.fillSmoothRoundRect(x, y, w, h, radius, Colors::CARD_BORDER_ACCENT);
    // Interior fill (slightly lighter than standard card)
    spr.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, radius - 1, Colors::BG_CARD);
    // Top highlight
    int hlX = x + radius;
    int hlW = w - 2 * radius;
    if (hlW > 0) {
        spr.drawFastHLine(hlX, y + 1, hlW, Colors::CARD_HIGHLIGHT);
    }
}

// ══════════════════════════════════════════
//  LEGACY CARD
// ══════════════════════════════════════════

void drawCard(LGFX_Sprite& spr, int x, int y, int w, int h, int radius,
              uint16_t bgColor, uint16_t borderColor) {
    spr.fillSmoothRoundRect(x, y, w, h, radius, borderColor);
    spr.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, radius - 1, bgColor);
}

// ══════════════════════════════════════════
//  SPARKLINE
// ══════════════════════════════════════════

void drawSparkline(LGFX_Sprite& spr, int x, int y, int w, int h,
                   const SparklineData& data, uint16_t lineColor, uint16_t fillColor) {
    if (!data.valid || data.count < 2) return;

    float range = data.maxVal - data.minVal;
    if (range < 0.01f) range = 1.0f;

    auto mapX = [&](int i) -> int {
        return x + (i * w) / (data.count - 1);
    };
    auto mapY = [&](float val) -> int {
        return y + h - 1 - (int)(((val - data.minVal) / range) * (h - 2));
    };

    int bottom = y + h - 1;

    // Filled area with vertical gradient
    for (int i = 0; i < data.count - 1; i++) {
        int x0 = mapX(i);
        int x1 = mapX(i + 1);
        int y0 = mapY(data.points[i]);
        int y1 = mapY(data.points[i + 1]);

        for (int px = x0; px <= x1; px++) {
            float t = (x1 == x0) ? 0 : (float)(px - x0) / (x1 - x0);
            int lineY = y0 + (int)(t * (y1 - y0));
            if (lineY < bottom) {
                int fillH = bottom - lineY;
                for (int fy = lineY + 1; fy <= bottom; fy++) {
                    float grad = (float)(fy - lineY) / fillH;
                    uint16_t c = lerpColor565(fillColor, Colors::BG_CARD, grad);
                    spr.drawPixel(px, fy, c);
                }
            }
        }
    }

    // 2px thick line on top
    for (int i = 0; i < data.count - 1; i++) {
        int x0 = mapX(i);
        int x1 = mapX(i + 1);
        int y0 = mapY(data.points[i]);
        int y1 = mapY(data.points[i + 1]);
        spr.drawWideLine(x0, y0, x1, y1, 2.0f, lineColor);
    }

    // Last price dot with glow
    int lastX = mapX(data.count - 1);
    int lastY = mapY(data.points[data.count - 1]);
    spr.fillSmoothCircle(lastX, lastY, 5, Colors::GLOW_1);
    spr.fillSmoothCircle(lastX, lastY, 3, lineColor);
}

// ══════════════════════════════════════════
//  CANDLESTICK CHART
// ══════════════════════════════════════════

void drawCandlestick(LGFX_Sprite& spr, int x, int y, int w, int h,
                     const OhlcData& data) {
    if (!data.valid || data.count < 2) return;

    float range = data.maxVal - data.minVal;
    if (range < 0.01f) range = 1.0f;

    auto mapY = [&](float val) -> int {
        return y + h - 1 - (int)(((val - data.minVal) / range) * (h - 2));
    };

    float barSlot = (float)w / data.count;
    int bodyW = (int)(barSlot * 0.7f);
    if (bodyW < 1) bodyW = 1;
    if (bodyW > 8) bodyW = 8;
    int gap = (barSlot > 2) ? 1 : 0;

    for (int i = 0; i < data.count; i++) {
        const OhlcBar& bar = data.bars[i];
        int cx = x + (int)(i * barSlot + barSlot * 0.5f);

        int yHigh = mapY(bar.high);
        int yLow  = mapY(bar.low);
        int yOpen = mapY(bar.open);
        int yClose = mapY(bar.close);

        bool bullish = (bar.close >= bar.open);
        uint16_t bodyColor = bullish ? Colors::CANDLE_BULL : Colors::CANDLE_BEAR;

        // Wick (thin vertical line from high to low)
        spr.drawFastVLine(cx, yHigh, yLow - yHigh + 1, Colors::CANDLE_WICK);

        // Body (open to close rect)
        int bodyTop = bullish ? yClose : yOpen;
        int bodyBot = bullish ? yOpen  : yClose;
        int bodyH = bodyBot - bodyTop;
        if (bodyH < 1) bodyH = 1;

        int bodyX = cx - bodyW / 2;
        spr.fillRect(bodyX, bodyTop, bodyW, bodyH, bodyColor);
    }

    // Last price dot with glow
    if (data.count > 0) {
        const OhlcBar& last = data.bars[data.count - 1];
        int lastX = x + (int)((data.count - 1) * barSlot + barSlot * 0.5f);
        int lastY = mapY(last.close);
        bool bull = last.close >= last.open;
        uint16_t dotColor = bull ? Colors::CANDLE_BULL : Colors::CANDLE_BEAR;
        spr.fillSmoothCircle(lastX, lastY, 5, Colors::GLOW_1);
        spr.fillSmoothCircle(lastX, lastY, 3, dotColor);
    }
}

// ══════════════════════════════════════════
//  CHART MARKERS (high/low triangles + ATH line)
// ══════════════════════════════════════════

void drawChartMarkers(LGFX_Sprite& spr, int x, int y, int w, int h,
                      const SparklineData& data, float athPrice) {
    if (!data.valid || data.count < 2) return;

    float range = data.maxVal - data.minVal;
    if (range < 0.01f) range = 1.0f;

    auto mapX = [&](int i) -> int {
        return x + (i * w) / (data.count - 1);
    };
    auto mapY = [&](float val) -> int {
        return y + h - 1 - (int)(((val - data.minVal) / range) * (h - 2));
    };

    // Helper: format price with comma separator
    auto fmtPrice = [](char* buf, size_t sz, float price) {
        int v = (int)price;
        if (v >= 1000)
            snprintf(buf, sz, "$%d,%03d", v / 1000, v % 1000);
        else
            snprintf(buf, sz, "$%d", v);
    };

    // Helper: draw price label with dark pill background
    auto drawPricePill = [&](int cx, int cy, const char* text, uint16_t color, bool above) {
        int tw = spr.textWidth(text, &Satoshi12);
        int pillW = tw + 10;
        int pillH = 16;
        int pillX = cx - pillW / 2;
        int pillY = above ? (cy - pillH) : cy;

        // Clamp to chart bounds
        if (pillX < x) pillX = x;
        if (pillX + pillW > x + w) pillX = x + w - pillW;
        if (pillY < y) pillY = y;
        if (pillY + pillH > y + h) pillY = y + h - pillH;

        spr.fillSmoothRoundRect(pillX, pillY, pillW, pillH, 4, Colors::BG_SURFACE);
        spr.drawRoundRect(pillX, pillY, pillW, pillH, 4, color);
        spr.setTextColor(color, Colors::BG_SURFACE);
        spr.setTextDatum(lgfx::middle_center);
        spr.drawString(text, pillX + pillW / 2, pillY + pillH / 2, &Satoshi12);
    };

    // Find period high and low indices
    int highIdx = 0, lowIdx = 0;
    for (int i = 1; i < data.count; i++) {
        if (data.points[i] > data.points[highIdx]) highIdx = i;
        if (data.points[i] < data.points[lowIdx])  lowIdx = i;
    }

    // Period HIGH marker — green upward triangle + pill label
    {
        int hx = mapX(highIdx);
        int hy = mapY(data.points[highIdx]);
        int tri = 7;
        spr.fillTriangle(hx, hy - tri - 2, hx - tri, hy + 3, hx + tri, hy + 3, Colors::MARKER_HIGH);

        char buf[16];
        fmtPrice(buf, sizeof(buf), data.points[highIdx]);
        int labelY = hy - tri - 4;
        bool above = (labelY - 16 >= y);
        drawPricePill(hx, above ? labelY - 16 : hy + tri + 5, buf, Colors::MARKER_HIGH, false);
    }

    // Period LOW marker — red downward triangle + pill label
    {
        int lx = mapX(lowIdx);
        int ly = mapY(data.points[lowIdx]);
        int tri = 7;
        spr.fillTriangle(lx, ly + tri + 2, lx - tri, ly - 3, lx + tri, ly - 3, Colors::MARKER_LOW);

        char buf[16];
        fmtPrice(buf, sizeof(buf), data.points[lowIdx]);
        int labelY = ly + tri + 5;
        bool below = (labelY + 16 <= y + h);
        drawPricePill(lx, below ? labelY : ly - tri - 5 - 16, buf, Colors::MARKER_LOW, false);
    }

    // ATH dashed horizontal line (only if ATH is within visible Y range)
    if (athPrice > 0 && athPrice >= data.minVal && athPrice <= data.maxVal * 1.05f) {
        int athY = mapY(athPrice);
        if (athY >= y && athY < y + h) {
            // Dashed line (2px thick for visibility)
            for (int px = x; px < x + w; px += 8) {
                int dashW = 4;
                if (px + dashW > x + w) dashW = x + w - px;
                spr.fillRect(px, athY - 1, dashW, 2, Colors::ATH_LINE);
            }
            // ATH pill label
            char athBuf[20];
            fmtPrice(athBuf, sizeof(athBuf), athPrice);
            // Prepend "ATH "
            char fullBuf[28];
            snprintf(fullBuf, sizeof(fullBuf), "ATH %s", athBuf);
            drawPricePill(x + w - 40, athY - 20, fullBuf, Colors::ATH_LINE, false);
        }
    }
}

// ══════════════════════════════════════════
//  CHART STYLE BADGE
// ══════════════════════════════════════════

void drawChartStyleBadge(LGFX_Sprite& spr, int x, int y, ChartStyle style) {
    static const char* labels[] = { "LINE", "OHLC", "MKR" };
    const char* label = labels[style];

    int badgeW = 38;
    int badgeH = 16;
    int r = badgeH / 2;

    spr.fillSmoothRoundRect(x, y, badgeW, badgeH, r, Colors::BG_ELEVATED);
    spr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_ELEVATED);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString(label, x + badgeW / 2, y + badgeH / 2, &Satoshi9);
}

// ══════════════════════════════════════════
//  DOMINANCE BAR
// ══════════════════════════════════════════

void drawDominanceBar(LGFX_Sprite& spr, int x, int y, int w, int h,
                      float btcPct, float ethPct,
                      uint16_t btcColor, uint16_t ethColor, uint16_t otherColor) {
    int btcW = (int)(btcPct / 100.0f * w);
    int ethW = (int)(ethPct / 100.0f * w);
    int r = h / 2;

    // 1. Full background bar (rounded both ends) in otherColor
    spr.fillSmoothRoundRect(x, y, w, h, r, otherColor);

    // 2. BTC — flat rect covering left area
    if (btcW > 0) {
        spr.fillRect(x, y, btcW, h, btcColor);
    }

    // 3. ETH — flat rect in middle
    if (ethW > 0) {
        spr.fillRect(x + btcW, y, ethW, h, ethColor);
    }

    // 4. Re-round left cap (BTC color)
    if (btcW >= r) {
        spr.fillSmoothRoundRect(x, y, r * 2, h, r, btcColor);
    }
}

// ══════════════════════════════════════════
//  CHANGE BADGE (pill, tinted background)
// ══════════════════════════════════════════

void drawChangeBadge(LGFX_Sprite& spr, int x, int y, int w, int h,
                     float pct, const char* label, bool selected) {
    bool hasData = !isnan(pct);
    bool positive = hasData ? (pct >= 0) : true;
    uint16_t textColor = positive ? Colors::POSITIVE : Colors::NEGATIVE;
    uint16_t bgColor   = hasData ? (positive ? Colors::BADGE_BG_POS : Colors::BADGE_BG_NEG)
                                 : Colors::BG_ELEVATED;

    int r = h / 2;

    // Selected: draw green border first, then smaller fill
    if (selected) {
        spr.fillSmoothRoundRect(x, y, w, h, r, Colors::LEMON_GREEN);
        spr.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, bgColor);
    } else {
        spr.fillSmoothRoundRect(x, y, w, h, r, bgColor);
    }

    if (!hasData) {
        // No percentage data — draw just the label centered
        spr.setTextColor(Colors::TEXT_PRIMARY, bgColor);
        spr.setTextDatum(lgfx::middle_center);
        spr.drawString(label, x + w / 2, y + h / 2, &Satoshi9);
        return;
    }

    // Label at top ("1h", "24h", "7d", etc.) — white for readability
    spr.setTextColor(Colors::TEXT_PRIMARY, bgColor);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString(label, x + w / 2, y + h / 2 - 7, &Satoshi9);

    // Percentage value below
    char buf[20];
    snprintf(buf, sizeof(buf), "%s%.1f%%", positive ? "+" : "", pct);
    spr.setTextColor(textColor, bgColor);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString(buf, x + w / 2, y + h / 2 + 7, &Satoshi9);
}

// ══════════════════════════════════════════
//  COIN DOT (colored circle for each coin)
// ══════════════════════════════════════════

void drawCoinDot(LGFX_Sprite& spr, int cx, int cy, int r, CoinId coin) {
    uint16_t color = coinAccentColor(coin);
    spr.fillSmoothCircle(cx, cy, r, color);
}

// ══════════════════════════════════════════
//  CRYPTO CARD (mini card for 2x2 grid)
// ══════════════════════════════════════════

void drawCryptoCard(LGFX_Sprite& spr, int x, int y, int w, int h,
                    const CoinData& data, CoinId coin) {
    drawGlassCard(spr, x, y, w, h, 12);

    int pad = 10;
    uint16_t accent = coinAccentColor(coin);

    if (!data.valid) {
        spr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        spr.setTextDatum(lgfx::middle_center);
        spr.drawString("...", x + w / 2, y + h / 2, &Satoshi12);
        return;
    }

    // Coin dot + symbol
    drawCoinDot(spr, x + pad + 5, y + pad + 5, 5, coin);
    spr.setTextColor(accent, Colors::BG_CARD);
    spr.setTextDatum(lgfx::top_left);
    spr.drawString(data.symbol, x + pad + 16, y + pad, &Satoshi12);

    // Price
    char priceBuf[20];
    formatCoinPrice(priceBuf, sizeof(priceBuf), data.priceUsd, coin);
    spr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
    spr.setTextDatum(lgfx::top_left);
    spr.drawString(priceBuf, x + pad, y + pad + 22, &SatoshiMedium18);

    // 24h change badge (compact)
    float pct = data.change24h;
    bool positive = pct >= 0;
    uint16_t badgeBg = positive ? Colors::BADGE_BG_POS : Colors::BADGE_BG_NEG;
    uint16_t badgeTxt = positive ? Colors::POSITIVE : Colors::NEGATIVE;

    char changeBuf[16];
    snprintf(changeBuf, sizeof(changeBuf), "%s%.1f%%", positive ? "+" : "", pct);

    int badgeW = 64;
    int badgeH = 18;
    int badgeX = x + pad;
    int badgeY = y + h - pad - badgeH;
    spr.fillSmoothRoundRect(badgeX, badgeY, badgeW, badgeH, badgeH / 2, badgeBg);
    spr.setTextColor(badgeTxt, badgeBg);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString(changeBuf, badgeX + badgeW / 2, badgeY + badgeH / 2, &Satoshi9);
}

// ══════════════════════════════════════════
//  HELPERS
// ══════════════════════════════════════════

void drawCentered(LGFX_Sprite& spr, const char* text, int y, const lgfx::IFont* font, uint16_t color, uint16_t bg) {
    spr.setTextColor(color, bg);
    spr.setTextDatum(lgfx::top_center);
    spr.drawString(text, SCREEN_W / 2, y, font);
}

// ══════════════════════════════════════════
//  FORMATTERS
// ══════════════════════════════════════════

void formatBtcPrice(char* buf, size_t bufSize, float price) {
    int intPart = (int)price;
    if (intPart >= 1000) {
        snprintf(buf, bufSize, "$%d,%03d", intPart / 1000, intPart % 1000);
    } else {
        snprintf(buf, bufSize, "$%d", intPart);
    }
}

void formatArsPrice(char* buf, size_t bufSize, float price) {
    // Argentine format: $1.234,50 (dots for thousands, comma for decimals)
    int intPart = (int)price;
    int decPart = (int)((price - intPart) * 100 + 0.5f);
    if (decPart >= 100) { intPart++; decPart = 0; }

    if (intPart >= 1000000) {
        snprintf(buf, bufSize, "$%d.%03d.%03d,%02d",
                 intPart / 1000000, (intPart / 1000) % 1000, intPart % 1000, decPart);
    } else if (intPart >= 1000) {
        snprintf(buf, bufSize, "$%d.%03d,%02d",
                 intPart / 1000, intPart % 1000, decPart);
    } else {
        snprintf(buf, bufSize, "$%d,%02d", intPart, decPart);
    }
}

void formatCoinPrice(char* buf, size_t bufSize, float price, CoinId coin) {
    switch (coin) {
        case COIN_BTC:
            formatBtcPrice(buf, bufSize, price);
            break;
        case COIN_ETH:
        case COIN_SOL:
            // Alts: show integer with comma separator
            {
                int intPart = (int)price;
                if (intPart >= 1000) {
                    snprintf(buf, bufSize, "$%d,%03d", intPart / 1000, intPart % 1000);
                } else {
                    snprintf(buf, bufSize, "$%.0f", price);
                }
            }
            break;
        case COIN_USDT:
        case COIN_USDC:
            // Stablecoins: show 2 decimal places
            snprintf(buf, bufSize, "$%.2f", price);
            break;
        default:
            snprintf(buf, bufSize, "$%.2f", price);
            break;
    }
}

// ══════════════════════════════════════════
//  SLIDER
// ══════════════════════════════════════════

void drawSlider(LGFX_Sprite& spr, int x, int y, int w, int h, float value) {
    if (value < 0) value = 0;
    if (value > 1) value = 1;
    int r = h / 2;
    // Track background
    spr.fillSmoothRoundRect(x, y, w, h, r, Colors::BG_ELEVATED);
    // Filled portion
    int fillW = (int)(value * w);
    if (fillW > h) {
        spr.fillSmoothRoundRect(x, y, fillW, h, r, Colors::LEMON_GREEN);
    }
    // Knob
    int knobX = x + fillW;
    if (knobX < x + r) knobX = x + r;
    if (knobX > x + w - r) knobX = x + w - r;
    spr.fillSmoothCircle(knobX, y + h / 2, h / 2 + 4, Colors::TEXT_PRIMARY);
    spr.fillSmoothCircle(knobX, y + h / 2, h / 2 + 1, Colors::LEMON_GREEN);
}

// ══════════════════════════════════════════
//  TOAST
// ══════════════════════════════════════════

static bool     toastActive    = false;
static char     toastMsg[64]   = "";
static uint32_t toastStartMs   = 0;
static const uint32_t TOAST_DURATION_MS = 3000;

void showToast(const char* msg) {
    strncpy(toastMsg, msg, sizeof(toastMsg) - 1);
    toastMsg[sizeof(toastMsg) - 1] = '\0';
    toastActive = true;
    toastStartMs = millis();
    // No direct tft draws — dashboard header will render the toast in sprZ0
}

void updateToast() {
    if (!toastActive) return;
    if (millis() - toastStartMs >= TOAST_DURATION_MS) {
        toastActive = false;
        // No direct tft draws — caller will redraw header to clear toast
    }
}

bool isToastActive() {
    return toastActive;
}

const char* getToastMessage() {
    return toastMsg;
}

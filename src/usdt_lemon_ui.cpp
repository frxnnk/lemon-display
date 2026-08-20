#include "usdt_lemon_ui.h"

#include "colors.h"
#include "config.h"
#include "display_manager.h"
#include "data/satoshi_fonts.h"
#include "data/lemon_v2_logo_light_120.h"
#include "data/usdt_logo_28.h"
#include "data/usdt_logo_64.h"
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
LGFX_Sprite s_canvas(&tft);
bool s_ready = false;
bool s_allocationAttempted = false;
UsdtLanguage s_uiLanguage = USDT_LANGUAGE_ES;

constexpr int SAFE = 24;
constexpr uint16_t TETHER_GREEN = 0x250F;
constexpr uint16_t TETHER_DARK = 0x0A29;
constexpr uint16_t ARGENTINA_BLUE = 0x5D9F;
constexpr uint16_t ARGENTINA_SUN = 0xFDC0;

const char* tr(UsdtLanguage language, const char* spanish, const char* english) {
    return language == USDT_LANGUAGE_EN ? english : spanish;
}

struct NetworkRow {
    const char* name;
    const char* tag;
};

constexpr NetworkRow PRIMARY_NETWORKS[] = {
    {"BNB CHAIN", "BEP20"},
    {"POLYGON", "MATIC"},
    {"TRON", "TRC20"},
    {"ETHEREUM", "ERC20"},
};

uint16_t freshnessColor(UsdtFreshness state) {
    if (state == USDT_LIVE) return TETHER_GREEN;
    if (state == USDT_RATE_LIMITED) return Colors::NEGATIVE;
    return Colors::TEXT_TERTIARY;
}

void drawLemonLogo(int x, int y) {
    for (int py = 0; py < 28; ++py) {
        for (int px = 0; px < 120; ++px) {
            const uint16_t color = pgm_read_word(&lemon_v2_logo_light_120[py * 120 + px]);
            if (color != 0x0000) s_canvas.drawPixel(x + px, y + py, color);
        }
    }
}

void drawTetherLogo(int x, int y) {
    for (int py = 0; py < 28; ++py) {
        for (int px = 0; px < 28; ++px) {
            const uint16_t color = pgm_read_word(&usdt_logo_28[py * 28 + px]);
            if (color != 0x0000) s_canvas.drawPixel(x + px, y + py, color);
        }
    }
}

void drawTetherLogo64(int x, int y) {
    for (int py = 0; py < 64; ++py) {
        for (int px = 0; px < 64; ++px) {
            const uint16_t color = pgm_read_word(&usdt_logo_64[py * 64 + px]);
            if (color != 0x0000) s_canvas.drawPixel(x + px, y + py, color);
        }
    }
}

void drawArgentinaFlag(int x, int y) {
    constexpr int width = 38;
    constexpr int height = 24;
    s_canvas.fillSmoothRoundRect(x, y, width, height, 4, ARGENTINA_BLUE);
    s_canvas.fillRect(x + 1, y + 8, width - 2, 8, 0xFFFF);
    s_canvas.fillCircle(x + width / 2, y + height / 2, 3, ARGENTINA_SUN);
    s_canvas.drawRoundRect(x, y, width, height, 4, Colors::CARD_BORDER);
}

void drawUnitedStatesFlag(int x, int y) {
    constexpr int width = 38;
    constexpr int height = 24;
    constexpr uint16_t red = 0xD945;
    constexpr uint16_t blue = 0x21D4;
    s_canvas.fillSmoothRoundRect(x, y, width, height, 4, 0xFFFF);
    for (int stripeY = 2; stripeY < height; stripeY += 4) {
        s_canvas.fillRect(x + 1, y + stripeY, width - 2, 2, red);
    }
    s_canvas.fillRect(x + 1, y + 1, 16, 12, blue);
    s_canvas.fillCircle(x + 5, y + 4, 1, 0xFFFF);
    s_canvas.fillCircle(x + 11, y + 4, 1, 0xFFFF);
    s_canvas.fillCircle(x + 8, y + 9, 1, 0xFFFF);
    s_canvas.fillCircle(x + 14, y + 9, 1, 0xFFFF);
    s_canvas.drawRoundRect(x, y, width, height, 4, Colors::CARD_BORDER);
}

void drawBrazilFlag(int x, int y) {
    constexpr int width = 38;
    constexpr int height = 24;
    constexpr uint16_t green = 0x1468;
    constexpr uint16_t yellow = 0xFFE0;
    constexpr uint16_t blue = 0x01D0;
    s_canvas.fillSmoothRoundRect(x, y, width, height, 4, green);
    s_canvas.fillTriangle(x + 5, y + height / 2,
                          x + width / 2, y + 3,
                          x + width / 2, y + height - 3, yellow);
    s_canvas.fillTriangle(x + width - 5, y + height / 2,
                          x + width / 2, y + 3,
                          x + width / 2, y + height - 3, yellow);
    s_canvas.fillCircle(x + width / 2, y + height / 2, 5, blue);
    s_canvas.drawRoundRect(x, y, width, height, 4, Colors::CARD_BORDER);
}

void drawPeruFlag(int x, int y) {
    constexpr int width = 38;
    constexpr int height = 24;
    constexpr uint16_t red = 0xD945;
    s_canvas.fillSmoothRoundRect(x, y, width, height, 4, red);
    s_canvas.fillRect(x + 13, y + 1, 12, height - 2, 0xFFFF);
    s_canvas.drawRoundRect(x, y, width, height, 4, Colors::CARD_BORDER);
}

void drawColombiaFlag(int x, int y) {
    constexpr int width = 38;
    constexpr int height = 24;
    constexpr uint16_t yellow = 0xFFE0;
    constexpr uint16_t blue = 0x01D0;
    constexpr uint16_t red = 0xD945;
    s_canvas.fillSmoothRoundRect(x, y, width, height, 4, yellow);
    s_canvas.fillRect(x + 1, y + 12, width - 2, 6, blue);
    s_canvas.fillRect(x + 1, y + 18, width - 2, 5, red);
    s_canvas.drawRoundRect(x, y, width, height, 4, Colors::CARD_BORDER);
}

void fillDiamond(int cx, int cy, int radius, uint16_t color) {
    s_canvas.fillTriangle(cx, cy - radius, cx - radius, cy,
                          cx + radius, cy, color);
    s_canvas.fillTriangle(cx, cy + radius, cx - radius, cy,
                          cx + radius, cy, color);
}

void drawBnbIcon(int x, int y) {
    constexpr uint16_t yellow = 0xFE60;
    fillDiamond(x + 12, y + 12, 5, yellow);
    fillDiamond(x + 12, y + 3, 3, yellow);
    fillDiamond(x + 3, y + 12, 3, yellow);
    fillDiamond(x + 21, y + 12, 3, yellow);
    fillDiamond(x + 12, y + 21, 3, yellow);
    fillDiamond(x + 12, y + 12, 2, Colors::BG_BASE);
}

void drawPolygonIcon(int x, int y) {
    constexpr uint16_t purple = 0x8A5F;
    s_canvas.drawRoundRect(x + 2, y + 7, 11, 10, 4, purple);
    s_canvas.drawRoundRect(x + 11, y + 7, 11, 10, 4, purple);
    s_canvas.drawLine(x + 9, y + 9, x + 15, y + 15, purple);
    s_canvas.drawLine(x + 9, y + 15, x + 15, y + 9, purple);
}

void drawTronIcon(int x, int y) {
    constexpr uint16_t red = 0xF926;
    s_canvas.fillTriangle(x + 3, y + 3, x + 21, y + 7,
                          x + 10, y + 22, red);
    s_canvas.fillTriangle(x + 7, y + 6, x + 17, y + 8,
                          x + 10, y + 17, Colors::BG_BASE);
    s_canvas.drawLine(x + 3, y + 3, x + 10, y + 17, red);
    s_canvas.drawLine(x + 21, y + 7, x + 10, y + 17, red);
}

void drawEthereumIcon(int x, int y) {
    constexpr uint16_t light = 0xBDF7;
    constexpr uint16_t dark = 0x6B6D;
    s_canvas.fillTriangle(x + 12, y + 1, x + 4, y + 13,
                          x + 12, y + 10, light);
    s_canvas.fillTriangle(x + 12, y + 1, x + 20, y + 13,
                          x + 12, y + 10, dark);
    s_canvas.fillTriangle(x + 12, y + 23, x + 4, y + 15,
                          x + 12, y + 18, dark);
    s_canvas.fillTriangle(x + 12, y + 23, x + 20, y + 15,
                          x + 12, y + 18, light);
}

void drawUsdtTitle(const char* title) {
    const int textWidth = s_canvas.textWidth(title, &SatoshiBold24);
    const int groupWidth = 28 + 12 + textWidth;
    const int groupX = (SCREEN_W - groupWidth) / 2;
    drawTetherLogo(groupX, 80);
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(TETHER_GREEN, Colors::BG_BASE);
    s_canvas.drawString(title, groupX + 40, 82, &SatoshiBold24);
}

void drawPill(const char* label, int x, int y, uint16_t color) {
    const int width = s_canvas.textWidth(label, &Satoshi9) + 20;
    s_canvas.fillSmoothRoundRect(x, y, width, 24, 12, Colors::BG_ELEVATED);
    s_canvas.drawRoundRect(x, y, width, 24, 12, color);
    s_canvas.setTextDatum(lgfx::middle_center);
    s_canvas.setTextColor(color, Colors::BG_ELEVATED);
    s_canvas.drawString(label, x + width / 2, y + 12, &Satoshi9);
}

const char* freshnessCopy(UsdtFreshness freshness, UsdtLanguage language) {
    switch (freshness) {
        case USDT_LIVE: return tr(language, "EN VIVO", "LIVE");
        case USDT_CACHED: return tr(language, "CACHE", "CACHED");
        case USDT_STALE: return tr(language, "DESACT.", "STALE");
        case USDT_OFFLINE: return tr(language, "SIN RED", "OFFLINE");
        case USDT_RATE_LIMITED: return tr(language, "REINTENTO", "RETRY");
        case USDT_ERROR: return tr(language, "REINTENTO", "RETRY");
        default: return tr(language, "CARGANDO", "LOADING");
    }
}

const char* fetchStatusCopy(UsdtFetchStatus status, UsdtLanguage language) {
    if (status == USDT_FETCH_OK) return "OK";
    if (status == USDT_FETCH_RATE_LIMITED) return tr(language, "LIMITADO", "LIMITED");
    return tr(language, "REINTENTO", "RETRY");
}

void drawHeader(const UsdtDataSnapshot& data, const UsdtDeviceInfo& device,
                const UsdtRuntimeModel& model) {
    drawLemonLogo(SAFE, 22);
    s_canvas.setTextDatum(lgfx::top_center);
    s_canvas.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    s_canvas.drawString(device.time, SCREEN_W / 2, 28, &Satoshi12);
    const UsdtFreshness overall =
        usdtPrimaryFreshness(data.lemonFreshness, data.pegFreshness);
    const char* label = freshnessCopy(overall, model.language);
    const int width = s_canvas.textWidth(label, &Satoshi9) + 20;
    drawPill(label, SCREEN_W - SAFE - width, 22, freshnessColor(overall));
    s_canvas.drawFastHLine(SAFE, 66, SCREEN_W - SAFE * 2, Colors::DIVIDER);
}

void formatArs(char* out, size_t outSize, float value) {
    if (!std::isfinite(value) || value <= 0.0f) {
        strncpy(out, "--", outSize);
        out[outSize - 1] = '\0';
        return;
    }
    snprintf(out, outSize, "$%.2f", value);
}

void formatPct(char* out, size_t outSize, float value) {
    if (!std::isfinite(value)) {
        strncpy(out, "--", outSize);
        out[outSize - 1] = '\0';
        return;
    }
    snprintf(out, outSize, "%+.2f%%", value);
}

void formatUsdSupply(char* out, size_t outSize, float value) {
    if (!std::isfinite(value) || value <= 0.0f) {
        strncpy(out, "--", outSize);
        out[outSize - 1] = '\0';
    } else if (value >= 1.0e9f) {
        snprintf(out, outSize, "$%.2fB", value / 1.0e9f);
    } else if (value >= 1.0e6f) {
        snprintf(out, outSize, "$%.1fM", value / 1.0e6f);
    } else {
        snprintf(out, outSize, "$%.0fK", value / 1.0e3f);
    }
}

void drawCardLoadingPulse(int x, int y) {
    const uint8_t phase = (millis() / 400UL) % 3;
    for (uint8_t i = 0; i < 3; ++i) {
        const int height = i == phase ? 14 : 7;
        const uint16_t color = i == phase ? TETHER_GREEN : Colors::CARD_BORDER;
        s_canvas.fillSmoothRoundRect(x + i * 12, y + 14 - height,
                                     7, height, 3, color);
    }
}

void drawVariationPeriods(int x, int y, uint8_t active) {
    static const char* periods[] = {"1H", "24H", "7D"};
    for (uint8_t i = 0; i < 3; ++i) {
        const int pillX = x + i * 31;
        const bool selected = i == active;
        const uint16_t bg = selected ? TETHER_DARK : Colors::BG_ELEVATED;
        const uint16_t color = selected ? TETHER_GREEN : Colors::TEXT_TERTIARY;
        s_canvas.fillSmoothRoundRect(pillX, y, 28, 18, 8, bg);
        s_canvas.drawRoundRect(pillX, y, 28, 18, 8,
                               selected ? TETHER_GREEN : Colors::CARD_BORDER);
        s_canvas.setTextDatum(lgfx::middle_center);
        s_canvas.setTextColor(color, bg);
        s_canvas.drawString(periods[i], pillX + 14, y + 9, &Satoshi9);
    }
}

void drawCard(int x, int y, int w, int h, const char* label, const char* value,
              const char* suffix, uint16_t valueColor, bool loading = false) {
    s_canvas.fillSmoothRoundRect(x, y, w, h, 12, Colors::BG_CARD);
    s_canvas.drawRoundRect(x, y, w, h, 12, Colors::CARD_BORDER);
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(TETHER_GREEN, Colors::BG_CARD);
    s_canvas.drawString(label, x + 14, y + 10, &Satoshi9);
    if (loading) {
        drawCardLoadingPulse(x + 14, y + 36);
    } else {
        s_canvas.setTextColor(valueColor, Colors::BG_CARD);
        s_canvas.drawString(value, x + 14, y + 32, &SatoshiBold24);
    }
    if (suffix) {
        s_canvas.setTextDatum(lgfx::bottom_right);
        s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        s_canvas.drawString(suffix, x + w - 12, y + h - 10, &Satoshi9);
    }
}

void variationCopy(const UsdtPegData& peg, uint32_t nowMs, char* value,
                   size_t valueSize, uint16_t& color) {
    const uint8_t idx = usdtVariationIndex(nowMs);
    const float amount = idx == 0 ? peg.change1h : idx == 1 ? peg.change24h : peg.change7d;
    const bool usable = usdtAuxDataUsable(
        peg.variationsValid, peg.variationsLastUpdateMs, nowMs);
    if (usable) formatPct(value, valueSize, amount);
    else strncpy(value, "--", valueSize);
    color = !usable ? Colors::TEXT_TERTIARY
          : amount < 0.0f ? Colors::NEGATIVE
          : TETHER_GREEN;
}

void drawOverview(const UsdtDataSnapshot& data, const UsdtRuntimeModel& model) {
    drawTetherLogo64((SCREEN_W - 64) / 2, 76);
    const uint32_t nowMs = millis();
    char price[24] = "--";
    if (data.lemon.valid) formatArs(price, sizeof(price), data.lemon.ars);
    s_canvas.setTextDatum(lgfx::top_center);
    s_canvas.setTextColor(data.lemon.valid ? Colors::TEXT_PRIMARY : Colors::TEXT_TERTIARY,
                          Colors::BG_BASE);
    s_canvas.drawString(price, SCREEN_W / 2, 146, &SatoshiBold40);
    const int priceWidth = s_canvas.textWidth(price, &SatoshiBold40);
    const int flagX = min(SCREEN_W - SAFE - 38,
                          SCREEN_W / 2 + priceWidth / 2 + 12);
    drawArgentinaFlag(flagX, 157);
    s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    s_canvas.drawString("ARS", SCREEN_W / 2, 190, &Satoshi9);

    char variation[20] = "--";
    uint16_t variationColor = Colors::TEXT_TERTIARY;
    variationCopy(data.peg, nowMs, variation, sizeof(variation), variationColor);
    const bool variationUsable = usdtAuxDataUsable(
        data.peg.variationsValid, data.peg.variationsLastUpdateMs, nowMs);
    s_canvas.fillSmoothRoundRect(SAFE, 216, 204, 82, 12, Colors::BG_CARD);
    s_canvas.drawRoundRect(SAFE, 216, 204, 82, 12, Colors::CARD_BORDER);
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(TETHER_GREEN, Colors::BG_CARD);
    s_canvas.drawString(tr(model.language, "VARIACION", "CHANGE"),
                        SAFE + 14, 226, &Satoshi9);
    drawVariationPeriods(SAFE + 101, 223, usdtVariationIndex(nowMs));
    if (data.fetching && !variationUsable) {
        drawCardLoadingPulse(SAFE + 14, 260);
    } else {
        s_canvas.setTextDatum(lgfx::top_left);
        s_canvas.setTextColor(variationColor, Colors::BG_CARD);
        s_canvas.drawString(variation, SAFE + 14, 256, &SatoshiBold24);
    }

    char yieldValue[20] = "--";
    const bool yieldUsable = usdtAuxDataUsable(
        data.yield.valid, data.yield.lastUpdateMs, nowMs);
    if (yieldUsable) snprintf(yieldValue, sizeof(yieldValue), "%.2f%%", data.yield.aprPercent);
    drawCard(252, 216, 204, 82,
             tr(model.language, "RENDIMIENTO", "YIELD"), yieldValue, "LEMON",
             yieldUsable ? TETHER_GREEN : Colors::TEXT_TERTIARY,
             data.fetching && !yieldUsable);

    char pegValue[20] = "--";
    uint16_t pegColor = Colors::TEXT_TERTIARY;
    const bool pegUsable = usdtAuxDataUsable(
        data.peg.valid, data.peg.lastUpdateMs, nowMs);
    if (pegUsable) {
        snprintf(pegValue, sizeof(pegValue), "%.4f", data.peg.usd);
        const float bps = (data.peg.usd - 1.0f) * 10000.0f;
        pegColor = std::fabs(bps) <= 25.0f ? TETHER_GREEN : Colors::NEGATIVE;
    }
    const bool pegLoading = data.fetching && !pegUsable;
    drawCard(SAFE, 310, 204, 82, "PEG USD", pegValue, nullptr,
             pegColor, pegLoading);

    char spreadValue[20] = "--";
    const bool spreadUsable = data.lemon.valid && data.lemon.ask > 0.0f;
    if (spreadUsable) {
        snprintf(spreadValue, sizeof(spreadValue), "%.2f%%",
                 (data.lemon.ask - data.lemon.bid) * 100.0f / data.lemon.ask);
    }
    drawCard(252, 310, 204, 82, "SPREAD ARS", spreadValue,
             tr(model.language, "COMPRA / VENTA", "BID / ASK"),
             spreadUsable ? Colors::TEXT_PRIMARY : Colors::TEXT_TERTIARY,
             data.fetching && !spreadUsable);
}

void drawNetworks(const UsdtDataSnapshot& data, const UsdtRuntimeModel& model) {
    drawUsdtTitle(tr(model.language, "REDES", "CHAINS"));
    s_canvas.setTextDatum(lgfx::top_center);
    s_canvas.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    s_canvas.drawString(tr(model.language, "CAMBIO 24H", "24H CHANGE"),
                        SCREEN_W / 2, 116, &Satoshi9);
    for (int i = 0; i < 4; ++i) {
        const int y = 145 + i * 65;
        const UsdtNetworkMetric& metric = data.networks.metrics[i];
        char supply[20] = "--";
        char change[20] = "--";
        if (metric.valid) {
            formatUsdSupply(supply, sizeof(supply), metric.supplyUsd);
            formatPct(change, sizeof(change), metric.change24h);
        }
        if (i == USDT_NETWORK_BNB) drawBnbIcon(SAFE, y - 2);
        else if (i == USDT_NETWORK_POLYGON) drawPolygonIcon(SAFE, y - 2);
        else if (i == USDT_NETWORK_TRON) drawTronIcon(SAFE, y - 2);
        else drawEthereumIcon(SAFE, y - 2);
        s_canvas.setTextDatum(lgfx::top_left);
        s_canvas.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
        s_canvas.drawString(PRIMARY_NETWORKS[i].name, SAFE + 36, y, &Satoshi12);
        s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
        s_canvas.drawString(PRIMARY_NETWORKS[i].tag, SAFE + 36, y + 22, &Satoshi9);
        s_canvas.setTextDatum(lgfx::middle_right);
        s_canvas.setTextColor(!metric.valid ? Colors::TEXT_TERTIARY
                              : metric.change24h < 0.0f ? Colors::NEGATIVE
                              : TETHER_GREEN,
                              Colors::BG_BASE);
        s_canvas.drawString(change, SAFE + 230, y + 25, &Satoshi12);
        s_canvas.setTextDatum(lgfx::top_right);
        s_canvas.setTextColor(metric.valid ? Colors::TEXT_PRIMARY : Colors::TEXT_TERTIARY,
                              Colors::BG_BASE);
        s_canvas.drawString(supply, SCREEN_W - SAFE, y, &SatoshiBold24);
        s_canvas.drawFastHLine(SAFE + 36, y + 58, 396, Colors::DIVIDER);
    }
}

void drawMarketPairCard(int x, const char* label, const char* value,
                        const char* suffix, bool selected, bool loading,
                        const char* activeLabel) {
    constexpr int y = USDT_MARKET_CARD_Y;
    constexpr int w = USDT_MARKET_CARD_W;
    constexpr int h = USDT_MARKET_CARD_H;
    const uint16_t bg = selected ? TETHER_DARK : Colors::BG_CARD;
    s_canvas.fillSmoothRoundRect(x, y, w, h, 12, bg);
    s_canvas.drawRoundRect(x, y, w, h, 12,
                           selected ? TETHER_GREEN : Colors::CARD_BORDER);
    drawTetherLogo(x + 14, y + 10);
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(TETHER_GREEN, bg);
    s_canvas.drawString(label, x + 50, y + 15, &Satoshi9);
    if (loading) {
        drawCardLoadingPulse(x + 14, y + 48);
    } else {
        s_canvas.setTextColor(Colors::TEXT_PRIMARY, bg);
        s_canvas.drawString(value, x + 14, y + 50, &SatoshiBold24);
    }
    if (selected && activeLabel) {
        s_canvas.setTextDatum(lgfx::bottom_left);
        s_canvas.setTextColor(TETHER_GREEN, bg);
        s_canvas.drawString(activeLabel, x + 14, y + h - 10, &Satoshi9);
    }
    s_canvas.setTextDatum(lgfx::bottom_right);
    s_canvas.setTextColor(Colors::TEXT_TERTIARY, bg);
    s_canvas.drawString(suffix, x + w - 12, y + h - 10, &Satoshi9);
}

void drawMarketChart(const float* values, uint8_t count, bool usable,
                     bool loading, UsdtMarketPair pair,
                     UsdtLanguage language) {
    constexpr int x = SAFE;
    constexpr int y = 260;
    constexpr int w = 432;
    constexpr int h = 120;
    constexpr int chartX = x + 14;
    constexpr int chartY = y + 32;
    constexpr int chartW = w - 28;
    constexpr int chartH = 48;

    s_canvas.fillSmoothRoundRect(x, y, w, h, 12, Colors::BG_CARD);
    s_canvas.drawRoundRect(x, y, w, h, 12, Colors::CARD_BORDER);
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(TETHER_GREEN, Colors::BG_CARD);
    const bool usd = pair == USDT_MARKET_USD;
    s_canvas.drawString(usd ? "USDT / USD - 7D" : "USDT / ARS - 7D",
                        x + 14, y + 10, &Satoshi9);

    if (!usable || count < 2) {
        if (loading) {
            drawCardLoadingPulse(x + 14, y + 48);
        } else {
            s_canvas.setTextDatum(lgfx::middle_center);
            s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
            s_canvas.drawString("--", x + w / 2, chartY + chartH / 2, &SatoshiBold24);
        }
        return;
    }

    float minPrice = values[0];
    float maxPrice = values[0];
    for (uint8_t i = 1; i < count; ++i) {
        minPrice = min(minPrice, values[i]);
        maxPrice = max(maxPrice, values[i]);
    }
    float range = maxPrice - minPrice;
    const float minimumRange = usd ? 0.0005f : 0.01f;
    if (range < minimumRange) range = minimumRange;

    s_canvas.drawFastHLine(chartX, chartY + chartH / 2, chartW, Colors::DIVIDER);
    const uint16_t chartColor =
        values[count - 1] >= values[0]
            ? TETHER_GREEN
            : Colors::NEGATIVE;
    int previousX = chartX;
    int previousY = chartY + chartH - 1 - static_cast<int>(
        (values[0] - minPrice) * (chartH - 1) / range);
    for (uint8_t i = 1; i < count; ++i) {
        const int pointX = chartX +
            static_cast<int>(i) * (chartW - 1) / (count - 1);
        const int pointY = chartY + chartH - 1 - static_cast<int>(
            (values[i] - minPrice) * (chartH - 1) / range);
        s_canvas.drawLine(previousX, previousY, pointX, pointY, chartColor);
        previousX = pointX;
        previousY = pointY;
    }

    char minValue[20];
    char maxValue[20];
    char currentValue[20];
    char minLabel[28];
    char maxLabel[28];
    char currentLabel[32];
    if (usd) {
        snprintf(minValue, sizeof(minValue), "%.4f", minPrice);
        snprintf(maxValue, sizeof(maxValue), "%.4f", maxPrice);
        snprintf(currentValue, sizeof(currentValue), "%.4f", values[count - 1]);
    } else {
        formatArs(minValue, sizeof(minValue), minPrice);
        formatArs(maxValue, sizeof(maxValue), maxPrice);
        formatArs(currentValue, sizeof(currentValue), values[count - 1]);
    }
    snprintf(minLabel, sizeof(minLabel), "MIN %s", minValue);
    snprintf(maxLabel, sizeof(maxLabel), "MAX %s", maxValue);
    snprintf(currentLabel, sizeof(currentLabel), "%s %s",
             tr(language, "ACTUAL", "CURRENT"), currentValue);
    s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
    s_canvas.setTextDatum(lgfx::bottom_left);
    s_canvas.drawString(minLabel, x + 14, y + h - 10, &Satoshi9);
    s_canvas.setTextDatum(lgfx::bottom_center);
    s_canvas.setTextColor(chartColor, Colors::BG_CARD);
    s_canvas.drawString(currentLabel, x + w / 2, y + h - 10, &Satoshi9);
    s_canvas.setTextDatum(lgfx::bottom_right);
    s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
    s_canvas.drawString(maxLabel, x + w - 14, y + h - 10, &Satoshi9);
}

void drawMarkets(const UsdtDataSnapshot& data, const UsdtRuntimeModel& model) {
    drawUsdtTitle("MARKETS");
    char ars[20] = "--";
    char usd[20] = "--";
    formatArs(ars, sizeof(ars), data.lemon.ars);
    const bool pegUsable = usdtAuxDataUsable(
        data.peg.valid, data.peg.lastUpdateMs, millis());
    const bool arsChartUsable = usdtAuxDataUsable(
        data.peg.variationsValid, data.peg.variationsLastUpdateMs, millis());
    const bool usdChartUsable = usdtAuxDataUsable(
        data.peg.usdChartValid, data.peg.usdChartLastUpdateMs, millis());
    if (pegUsable) snprintf(usd, sizeof(usd), "%.4f", data.peg.usd);
    const bool arsSelected = model.marketPair == USDT_MARKET_ARS;
    const bool usdSelected = model.marketPair == USDT_MARKET_USD;
    const char* activeLabel = tr(model.language, "ACTIVO", "ACTIVE");
    drawMarketPairCard(USDT_MARKET_ARS_X, "USDT / ARS", ars, "LEMON",
                       arsSelected, data.fetching && !data.lemon.valid,
                       arsSelected ? activeLabel : nullptr);
    drawMarketPairCard(USDT_MARKET_USD_X, "USDT / USD", usd, "PEG",
                       usdSelected, data.fetching && !pegUsable,
                       usdSelected ? activeLabel : nullptr);
    drawArgentinaFlag(176, 140);
    drawUnitedStatesFlag(404, 140);
    if (arsSelected) {
        drawMarketChart(data.peg.arsChart, data.peg.arsChartCount,
                        arsChartUsable, data.fetching && !arsChartUsable,
                        USDT_MARKET_ARS, model.language);
    } else {
        drawMarketChart(data.peg.usdChart, data.peg.usdChartCount,
                        usdChartUsable, data.fetching && !usdChartUsable,
                        USDT_MARKET_USD, model.language);
    }
}

void drawRegions(const UsdtDataSnapshot& data, const UsdtRuntimeModel& model) {
    drawUsdtTitle(tr(model.language, "REGIONES", "REGIONS"));
    char ars[20] = "--";
    char brl[20] = "--";
    char pen[20] = "--";
    char cop[20] = "--";
    formatArs(ars, sizeof(ars), data.lemon.valid ? data.lemon.ars : data.peg.ars);
    const bool regionsUsable = usdtAuxDataUsable(
        data.peg.regionsValid, data.peg.regionsLastUpdateMs, millis());
    const bool arsUsable = data.lemon.valid ||
        (data.peg.variationsValid && data.peg.ars > 0.0f);
    if (regionsUsable) {
        snprintf(brl, sizeof(brl), "%.2f", data.peg.brl);
        snprintf(pen, sizeof(pen), "%.2f", data.peg.pen);
        snprintf(cop, sizeof(cop), "%.0f", data.peg.cop);
    }
    drawCard(SAFE, 128, 204, 120, "ARGENTINA", ars, "ARS", Colors::TEXT_PRIMARY,
             data.fetching && !arsUsable);
    drawCard(252, 128, 204, 120, tr(model.language, "BRASIL", "BRAZIL"),
             brl, "BRL", Colors::TEXT_PRIMARY,
             data.fetching && !regionsUsable);
    drawCard(SAFE, 260, 204, 120, "PERU", pen, "PEN", Colors::TEXT_PRIMARY,
             data.fetching && !regionsUsable);
    drawCard(252, 260, 204, 120, "COLOMBIA", cop, "COP", Colors::TEXT_PRIMARY,
             data.fetching && !regionsUsable);
    drawArgentinaFlag(176, 140);
    drawBrazilFlag(404, 140);
    drawPeruFlag(176, 272);
    drawColombiaFlag(404, 272);
}

void drawSystemControl(int y, const char* label, const char* value,
                       bool emphasized = false) {
    const uint16_t bg = emphasized ? TETHER_DARK : Colors::BG_CARD;
    s_canvas.fillSmoothRoundRect(USDT_SYSTEM_CONTROL_X, y,
                                 USDT_SYSTEM_CONTROL_W, USDT_SYSTEM_CONTROL_H,
                                 12, bg);
    s_canvas.drawRoundRect(USDT_SYSTEM_CONTROL_X, y,
                           USDT_SYSTEM_CONTROL_W, USDT_SYSTEM_CONTROL_H,
                           12, emphasized ? TETHER_GREEN : Colors::CARD_BORDER);
    s_canvas.setTextDatum(lgfx::middle_left);
    s_canvas.setTextColor(emphasized ? TETHER_GREEN : Colors::TEXT_PRIMARY, bg);
    s_canvas.drawString(label, USDT_SYSTEM_CONTROL_X + 16,
                        y + USDT_SYSTEM_CONTROL_H / 2, &Satoshi12);
    s_canvas.setTextDatum(lgfx::middle_right);
    s_canvas.setTextColor(TETHER_GREEN, bg);
    s_canvas.drawString(value,
                        USDT_SYSTEM_CONTROL_X + USDT_SYSTEM_CONTROL_W - 16,
                        y + USDT_SYSTEM_CONTROL_H / 2, &Satoshi12);
}

void drawSystem(const UsdtDataSnapshot& data, const UsdtDeviceInfo& device,
                const UsdtRuntimeModel& model) {
    drawUsdtTitle(tr(model.language, "SISTEMA", "SYSTEM"));
    char signal[20];
    snprintf(signal, sizeof(signal), "%ld dBm", static_cast<long>(device.rssi));
    const char* ota = data.ota.checking ? tr(model.language, "BUSCANDO", "CHECKING")
                     : data.ota.failed ? tr(model.language, "PAUSA 30M", "PAUSED 30M")
                     : data.ota.available ? data.ota.version
                     : data.ota.checked ? tr(model.language, "AL DIA", "UP TO DATE")
                     : tr(model.language, "PENDIENTE", "PENDING");
    s_canvas.fillSmoothRoundRect(SAFE, 122, 432, 72, 12, Colors::BG_CARD);
    s_canvas.drawRoundRect(SAFE, 122, 432, 72, 12, Colors::CARD_BORDER);
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
    s_canvas.drawString(tr(model.language, "VERSION", "VERSION"), 40, 136, &Satoshi9);
    s_canvas.drawString("WI-FI", 40, 164, &Satoshi9);
    s_canvas.drawString(tr(model.language, "DATOS", "DATA"), 250, 136, &Satoshi9);
    s_canvas.drawString("OTA", 250, 164, &Satoshi9);
    s_canvas.setTextDatum(lgfx::top_right);
    s_canvas.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
    s_canvas.drawString("v" APP_VERSION, 228, 134, &Satoshi12);
    s_canvas.drawString(data.online ? signal : "--", 228, 162, &Satoshi12);
    s_canvas.drawString(fetchStatusCopy(data.lemonStatus, model.language),
                        440, 134, &Satoshi12);
    s_canvas.drawString(ota, 440, 162, &Satoshi12);

    drawSystemControl(USDT_SYSTEM_SOUND_Y,
                      tr(model.language, "SONIDO", "SOUND"),
                      model.soundEnabled
                          ? tr(model.language, "ACTIVO", "ON")
                          : tr(model.language, "APAGADO", "OFF"));
    drawSystemControl(USDT_SYSTEM_LANGUAGE_Y,
                      tr(model.language, "IDIOMA", "LANGUAGE"),
                      model.language == USDT_LANGUAGE_EN ? "ENGLISH" : "ESPANOL");
    drawSystemControl(USDT_SYSTEM_WIFI_Y,
                      tr(model.language, "RECONFIGURAR WI-FI", "RECONFIGURE WI-FI"),
                      ">", true);
}

void drawHomeNavIcon(int x, int y, uint16_t color) {
    s_canvas.drawLine(x - 8, y, x, y - 7, color);
    s_canvas.drawLine(x, y - 7, x + 8, y, color);
    s_canvas.drawRect(x - 6, y, 12, 8, color);
}

void drawNetworksNavIcon(int x, int y, uint16_t color) {
    s_canvas.drawLine(x - 7, y + 5, x, y - 6, color);
    s_canvas.drawLine(x, y - 6, x + 7, y + 5, color);
    s_canvas.drawLine(x - 7, y + 5, x + 7, y + 5, color);
    s_canvas.fillCircle(x, y - 6, 2, color);
    s_canvas.fillCircle(x - 7, y + 5, 2, color);
    s_canvas.fillCircle(x + 7, y + 5, 2, color);
}

void drawMarketsNavIcon(int x, int y, uint16_t color) {
    s_canvas.drawFastVLine(x - 7, y - 5, 11, color);
    s_canvas.fillRect(x - 9, y - 2, 5, 5, color);
    s_canvas.drawFastVLine(x, y - 8, 15, color);
    s_canvas.fillRect(x - 2, y - 5, 5, 7, color);
    s_canvas.drawFastVLine(x + 7, y - 4, 11, color);
    s_canvas.fillRect(x + 5, y, 5, 5, color);
}

void drawRegionsNavIcon(int x, int y, uint16_t color) {
    s_canvas.drawCircle(x, y, 9, color);
    s_canvas.drawFastVLine(x, y - 8, 17, color);
    s_canvas.drawFastHLine(x - 8, y, 17, color);
    s_canvas.drawEllipse(x, y, 4, 9, color);
}

void drawSystemNavIcon(int x, int y, uint16_t color) {
    s_canvas.drawCircle(x, y, 7, color);
    s_canvas.fillCircle(x, y, 2, color);
    for (int i = 0; i < 4; ++i) {
        const int dx = i % 2 == 0 ? 0 : (i == 1 ? 10 : -10);
        const int dy = i % 2 == 0 ? (i == 0 ? -10 : 10) : 0;
        s_canvas.drawLine(x + dx * 7 / 10, y + dy * 7 / 10,
                          x + dx, y + dy, color);
    }
}

void drawNavigationIcon(UsdtScene scene, int x, int y, uint16_t color) {
    if (scene == USDT_OVERVIEW) drawHomeNavIcon(x, y, color);
    else if (scene == USDT_NETWORKS) drawNetworksNavIcon(x, y, color);
    else if (scene == USDT_MARKETS) drawMarketsNavIcon(x, y, color);
    else if (scene == USDT_REGIONS) drawRegionsNavIcon(x, y, color);
    else drawSystemNavIcon(x, y, color);
}

void drawNavigation(const UsdtRuntimeModel& model) {
    s_canvas.fillRect(0, USDT_NAV_Y, SCREEN_W, USDT_NAV_H, Colors::BG_CARD);
    const char* labels[] = {
        tr(model.language, "INICIO", "HOME"),
        tr(model.language, "REDES", "CHAINS"),
        "MKT",
        tr(model.language, "REG", "REG"),
        tr(model.language, "SIST.", "SYS.")
    };
    for (int i = 0; i < USDT_SCENE_COUNT; ++i) {
        const int x = i * 96;
        const bool active = i == static_cast<int>(model.scene);
        if (active) {
            s_canvas.fillSmoothRoundRect(x + 10, USDT_NAV_Y + 5, 76, 54, 12, TETHER_DARK);
        }
        const uint16_t color = active ? TETHER_GREEN : Colors::TEXT_TERTIARY;
        drawNavigationIcon(static_cast<UsdtScene>(i), x + 48, USDT_NAV_Y + 20, color);
        s_canvas.setTextDatum(lgfx::middle_center);
        s_canvas.setTextColor(color, active ? TETHER_DARK : Colors::BG_CARD);
        s_canvas.drawString(labels[i], x + 48, USDT_NAV_Y + 44, &Satoshi9);
    }
}

void drawFramebufferError() {
    tft.fillScreen(Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_center);
    tft.setTextColor(Colors::NEGATIVE, Colors::BG_BASE);
    tft.drawString(tr(s_uiLanguage, "ERROR DE MEMORIA", "MEMORY ERROR"),
                   SCREEN_W / 2, SCREEN_H / 2 - 14, &SatoshiBold24);
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    tft.drawString(tr(s_uiLanguage, "REINICIA LA LEMON BOX", "RESTART THE LEMON BOX"),
                   SCREEN_W / 2, SCREEN_H / 2 + 22, &Satoshi12);
}
}  // namespace

void usdtUiSetLanguage(UsdtLanguage language) {
    s_uiLanguage = language;
}

bool usdtUiSetup() {
    if (s_ready) return true;
    if (s_allocationAttempted) return false;
    s_allocationAttempted = true;
    s_canvas.setPsram(true);
    s_canvas.setColorDepth(16);
    s_ready = s_canvas.createSprite(SCREEN_W, SCREEN_H) != nullptr;
    if (!s_ready) {
        Serial.println("[USDt] Fatal: 480x480 framebuffer allocation failed");
        drawFramebufferError();
    }
    return s_ready;
}

void usdtUiDrawLoading(const char* message, uint8_t progress) {
    if (!usdtUiSetup()) return;
    s_canvas.fillSprite(Colors::BG_BASE);
    drawLemonLogo((SCREEN_W - 120) / 2, 168);
    s_canvas.setTextDatum(lgfx::middle_center);
    s_canvas.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    s_canvas.drawString(message, SCREEN_W / 2, 230, &Satoshi12);
    s_canvas.drawRoundRect(100, 270, 280, 8, 4, Colors::CARD_BORDER);
    s_canvas.fillRoundRect(100, 270, static_cast<int>(progress) * 280 / 100, 8, 4, TETHER_GREEN);
    displayWaitVSync();
    s_canvas.pushSprite(0, 0);
}

void usdtUiDraw(const UsdtDataSnapshot& data, const UsdtRuntimeModel& model,
                const UsdtDeviceInfo& device) {
    if (!usdtUiSetup()) return;
    s_canvas.fillSprite(Colors::BG_BASE);
    drawHeader(data, device, model);
    switch (model.scene) {
        case USDT_OVERVIEW: drawOverview(data, model); break;
        case USDT_NETWORKS: drawNetworks(data, model); break;
        case USDT_MARKETS: drawMarkets(data, model); break;
        case USDT_REGIONS: drawRegions(data, model); break;
        case USDT_SYSTEM: drawSystem(data, device, model); break;
        default: drawOverview(data, model); break;
    }
    drawNavigation(model);
    displayWaitVSync();
    s_canvas.pushSprite(0, 0);
}

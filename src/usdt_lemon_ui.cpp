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

constexpr int SAFE = 24;
constexpr uint16_t TETHER_GREEN = 0x250F;
constexpr uint16_t TETHER_DARK = 0x0A29;

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

void drawUsdtTitle(const char* title) {
    drawTetherLogo(SAFE, 80);
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(TETHER_GREEN, Colors::BG_BASE);
    s_canvas.drawString(title, SAFE + 40, 82, &SatoshiBold24);
}

void drawPill(const char* label, int x, int y, uint16_t color) {
    const int width = s_canvas.textWidth(label, &Satoshi9) + 20;
    s_canvas.fillSmoothRoundRect(x, y, width, 24, 12, Colors::BG_ELEVATED);
    s_canvas.drawRoundRect(x, y, width, 24, 12, color);
    s_canvas.setTextDatum(lgfx::middle_center);
    s_canvas.setTextColor(color, Colors::BG_ELEVATED);
    s_canvas.drawString(label, x + width / 2, y + 12, &Satoshi9);
}

void drawHeader(const UsdtDataSnapshot& data, const UsdtDeviceInfo& device) {
    drawLemonLogo(SAFE, 22);
    s_canvas.setTextDatum(lgfx::top_center);
    s_canvas.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    s_canvas.drawString(device.time, SCREEN_W / 2, 28, &Satoshi12);
    const UsdtFreshness overall =
        usdtPrimaryFreshness(data.lemonFreshness, data.pegFreshness);
    const char* label = usdtFreshnessLabel(overall);
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
    snprintf(out, outSize, "$%.0f", value);
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

void drawCard(int x, int y, int w, int h, const char* label, const char* value,
              const char* suffix, uint16_t valueColor) {
    s_canvas.fillSmoothRoundRect(x, y, w, h, 12, Colors::BG_CARD);
    s_canvas.drawRoundRect(x, y, w, h, 12, Colors::CARD_BORDER);
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(TETHER_GREEN, Colors::BG_CARD);
    s_canvas.drawString(label, x + 14, y + 10, &Satoshi9);
    s_canvas.setTextColor(valueColor, Colors::BG_CARD);
    s_canvas.drawString(value, x + 14, y + 32, &SatoshiBold24);
    if (suffix) {
        s_canvas.setTextDatum(lgfx::bottom_right);
        s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        s_canvas.drawString(suffix, x + w - 12, y + h - 10, &Satoshi9);
    }
}

void variationCopy(const UsdtPegData& peg, uint32_t nowMs, char* value, size_t valueSize,
                   char* suffix, size_t suffixSize, uint16_t& color) {
    const uint8_t idx = usdtVariationIndex(nowMs);
    const float amount = idx == 0 ? peg.change1h : idx == 1 ? peg.change24h : peg.change7d;
    const char* window = idx == 0 ? "1H" : idx == 1 ? "24H" : "7D";
    const bool usable = usdtAuxDataUsable(
        peg.variationsValid, peg.variationsLastUpdateMs, nowMs);
    if (usable) formatPct(value, valueSize, amount);
    else strncpy(value, "--", valueSize);
    snprintf(suffix, suffixSize, "%s ARS", window);
    color = !usable ? Colors::TEXT_TERTIARY
          : amount < 0.0f ? Colors::NEGATIVE
          : TETHER_GREEN;
}

void drawOverview(const UsdtDataSnapshot& data) {
    drawTetherLogo64((SCREEN_W - 64) / 2, 76);
    const uint32_t nowMs = millis();
    char price[24] = "--";
    if (data.lemon.valid) formatArs(price, sizeof(price), data.lemon.ars);
    s_canvas.setTextDatum(lgfx::top_center);
    s_canvas.setTextColor(data.lemon.valid ? Colors::TEXT_PRIMARY : Colors::TEXT_TERTIARY,
                          Colors::BG_BASE);
    s_canvas.drawString(price, SCREEN_W / 2, 146, &SatoshiBold40);
    s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    s_canvas.drawString("ARS", SCREEN_W / 2, 190, &Satoshi9);

    char variation[20] = "--";
    char variationSuffix[16] = "1H ARS";
    uint16_t variationColor = Colors::TEXT_TERTIARY;
    variationCopy(data.peg, nowMs, variation, sizeof(variation),
                  variationSuffix, sizeof(variationSuffix), variationColor);
    drawCard(SAFE, 216, 204, 82, "VARIACION", variation, variationSuffix, variationColor);

    char yieldValue[20] = "--";
    const bool yieldUsable = usdtAuxDataUsable(
        data.yield.valid, data.yield.lastUpdateMs, nowMs);
    if (yieldUsable) snprintf(yieldValue, sizeof(yieldValue), "%.2f%%", data.yield.aprPercent);
    drawCard(252, 216, 204, 82, "RENDIMIENTO", yieldValue, "LEMON YIELD",
             yieldUsable ? TETHER_GREEN : Colors::TEXT_TERTIARY);

    char pegValue[20] = "--";
    char pegSuffix[20] = "USD";
    uint16_t pegColor = Colors::TEXT_TERTIARY;
    const bool pegUsable = usdtAuxDataUsable(
        data.peg.valid, data.peg.lastUpdateMs, nowMs);
    if (pegUsable) {
        snprintf(pegValue, sizeof(pegValue), "%.4f", data.peg.usd);
        const float bps = (data.peg.usd - 1.0f) * 10000.0f;
        snprintf(pegSuffix, sizeof(pegSuffix), "%+.1f BPS", bps);
        pegColor = std::fabs(bps) <= 25.0f ? TETHER_GREEN : Colors::NEGATIVE;
    }
    drawCard(SAFE, 310, 432, 82, "PEG USD", pegValue, pegSuffix, pegColor);
}

void drawNetworks(const UsdtDataSnapshot& data) {
    drawUsdtTitle("REDES");
    s_canvas.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    s_canvas.drawString("USDT EN CIRCULACION  /  CAMBIO 24H", SAFE, 116, &Satoshi9);
    for (int i = 0; i < 4; ++i) {
        const int y = 142 + i * 55;
        const UsdtNetworkMetric& metric = data.networks.metrics[i];
        char supply[20] = "--";
        char change[20] = "--";
        if (metric.valid) {
            formatUsdSupply(supply, sizeof(supply), metric.supplyUsd);
            formatPct(change, sizeof(change), metric.change24h);
        }
        s_canvas.fillCircle(SAFE + 6, y + 9, 4,
                            metric.valid ? TETHER_GREEN : Colors::TEXT_TERTIARY);
        s_canvas.setTextDatum(lgfx::top_left);
        s_canvas.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
        s_canvas.drawString(PRIMARY_NETWORKS[i].name, SAFE + 22, y, &Satoshi12);
        s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
        s_canvas.drawString(PRIMARY_NETWORKS[i].tag, SAFE + 22, y + 22, &Satoshi9);
        s_canvas.setTextDatum(lgfx::top_right);
        s_canvas.setTextColor(metric.valid ? Colors::TEXT_PRIMARY : Colors::TEXT_TERTIARY,
                              Colors::BG_BASE);
        s_canvas.drawString(supply, SCREEN_W - SAFE, y, &SatoshiBold24);
        s_canvas.setTextColor(!metric.valid ? Colors::TEXT_TERTIARY
                              : metric.change24h < 0.0f ? Colors::NEGATIVE
                              : TETHER_GREEN,
                              Colors::BG_BASE);
        s_canvas.drawString(change, SCREEN_W - SAFE, y + 28, &Satoshi9);
        s_canvas.drawFastHLine(SAFE + 22, y + 49, 410, Colors::DIVIDER);
    }
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    s_canvas.drawString("SUPPLY ON-CHAIN  /  FUENTE: DEFILLAMA", SAFE, 372, &Satoshi9);
    s_canvas.setTextDatum(lgfx::top_right);
    s_canvas.setTextColor(TETHER_GREEN, Colors::BG_BASE);
    s_canvas.drawString(
        "Arbitrum / AVAX C-Chain / CELO / Monad / Optimism / Rootstock / Solana",
        SCREEN_W - SAFE, 390, &Satoshi9);
}

void drawMarkets(const UsdtDataSnapshot& data) {
    drawUsdtTitle("MARKETS");
    char ars[20] = "--";
    char usd[20] = "--";
    char change[20] = "--";
    char spread[20] = "--";
    formatArs(ars, sizeof(ars), data.lemon.ars);
    const bool pegUsable = usdtAuxDataUsable(
        data.peg.valid, data.peg.lastUpdateMs, millis());
    if (pegUsable) snprintf(usd, sizeof(usd), "%.4f", data.peg.usd);
    const bool variationsUsable = usdtAuxDataUsable(
        data.peg.variationsValid, data.peg.variationsLastUpdateMs, millis());
    if (variationsUsable) formatPct(change, sizeof(change), data.peg.change24h);
    if (data.lemon.valid && data.lemon.ask > 0.0f) {
        snprintf(spread, sizeof(spread), "%.2f%%",
                 (data.lemon.ask - data.lemon.bid) * 100.0f / data.lemon.ask);
    }
    drawCard(SAFE, 132, 204, 86, "USDT / ARS", ars, "LEMON", Colors::TEXT_PRIMARY);
    drawCard(252, 132, 204, 86, "USDT / USD", usd, "PEG", Colors::TEXT_PRIMARY);
    drawCard(SAFE, 232, 204, 86, "24H ARS", change, "VARIACION",
             !variationsUsable ? Colors::TEXT_TERTIARY
             : data.peg.change24h < 0 ? Colors::NEGATIVE : TETHER_GREEN);
    drawCard(252, 232, 204, 86, "SPREAD", spread, "BID / ASK", Colors::TEXT_PRIMARY);
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    s_canvas.drawString("DATOS DE MERCADO USDt", SAFE, 368, &Satoshi9);
}

void drawRegions(const UsdtDataSnapshot& data) {
    drawUsdtTitle("REGIONES");
    char ars[20] = "--";
    char brl[20] = "--";
    char pen[20] = "--";
    char cop[20] = "--";
    formatArs(ars, sizeof(ars), data.lemon.valid ? data.lemon.ars : data.peg.ars);
    const bool regionsUsable = usdtAuxDataUsable(
        data.peg.regionsValid, data.peg.regionsLastUpdateMs, millis());
    if (regionsUsable) {
        snprintf(brl, sizeof(brl), "%.2f", data.peg.brl);
        snprintf(pen, sizeof(pen), "%.2f", data.peg.pen);
        snprintf(cop, sizeof(cop), "%.0f", data.peg.cop);
    }
    drawCard(SAFE, 132, 204, 86, "ARGENTINA", ars, "ARS", Colors::TEXT_PRIMARY);
    drawCard(252, 132, 204, 86, "BRASIL", brl, "BRL", Colors::TEXT_PRIMARY);
    drawCard(SAFE, 232, 204, 86, "PERU", pen, "PEN", Colors::TEXT_PRIMARY);
    drawCard(252, 232, 204, 86, "COLOMBIA", cop, "COP", Colors::TEXT_PRIMARY);
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    s_canvas.drawString("COTIZACIONES REGIONALES", SAFE, 368, &Satoshi9);
}

void drawSystem(const UsdtDataSnapshot& data, const UsdtDeviceInfo& device) {
    s_canvas.setTextDatum(lgfx::top_left);
    s_canvas.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    s_canvas.drawString("SISTEMA", SAFE, 82, &SatoshiBold24);
    char signal[20];
    snprintf(signal, sizeof(signal), "%ld dBm", static_cast<long>(device.rssi));
    const char* ota = data.ota.checking ? "BUSCANDO"
                     : data.ota.available ? data.ota.version
                     : data.ota.checked ? "AL DIA"
                     : "PENDIENTE";
    static const char* labels[] = {
        "VERSION", "WI-FI", "PRECIO", "PEG / REG", "VARIACION",
        "YIELD", "REDES", "OTA"
    };
    const char* values[] = {
        "v" APP_VERSION,
        data.online ? signal : "--",
        usdtFetchStatusLabel(data.lemonStatus),
        usdtFetchStatusLabel(data.pegStatus),
        usdtFetchStatusLabel(data.variationsStatus),
        usdtFetchStatusLabel(data.yieldStatus),
        usdtFetchStatusLabel(data.networksStatus),
        ota
    };
    for (int i = 0; i < 8; ++i) {
        const int y = 116 + i * 27;
        s_canvas.setTextDatum(lgfx::top_left);
        s_canvas.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
        s_canvas.drawString(labels[i], SAFE, y, &Satoshi9);
        s_canvas.setTextDatum(lgfx::top_right);
        s_canvas.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
        s_canvas.drawString(values[i], SCREEN_W - SAFE, y, &Satoshi12);
    }
    s_canvas.fillSmoothRoundRect(USDT_SYSTEM_ACTION_X, USDT_SYSTEM_ACTION_Y,
                                 USDT_SYSTEM_ACTION_W, USDT_SYSTEM_ACTION_H,
                                 12, TETHER_DARK);
    s_canvas.setTextDatum(lgfx::middle_center);
    s_canvas.setTextColor(TETHER_GREEN, TETHER_DARK);
    s_canvas.drawString("RECONFIGURAR WI-FI",
                        USDT_SYSTEM_ACTION_X + USDT_SYSTEM_ACTION_W / 2,
                        USDT_SYSTEM_ACTION_Y + USDT_SYSTEM_ACTION_H / 2,
                        &SatoshiMedium18);
}

void drawNavigation(UsdtScene active) {
    s_canvas.fillRect(0, USDT_NAV_Y, SCREEN_W, USDT_NAV_H, Colors::BG_CARD);
    static const char* labels[] = {"INICIO", "REDES", "MKT", "REG", "SIST."};
    for (int i = 0; i < USDT_SCENE_COUNT; ++i) {
        const int x = i * 96;
        if (i == static_cast<int>(active)) {
            s_canvas.fillSmoothRoundRect(x + 8, USDT_NAV_Y + 8, 80, 42, 12, TETHER_DARK);
            s_canvas.fillRect(x + 30, USDT_NAV_Y + 54, 36, 3, TETHER_GREEN);
        }
        s_canvas.setTextDatum(lgfx::middle_center);
        s_canvas.setTextColor(i == static_cast<int>(active) ? TETHER_GREEN : Colors::TEXT_TERTIARY,
                              i == static_cast<int>(active) ? TETHER_DARK : Colors::BG_CARD);
        s_canvas.drawString(labels[i], x + 48, USDT_NAV_Y + 29, &Satoshi9);
    }
}

void drawFramebufferError() {
    tft.fillScreen(Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_center);
    tft.setTextColor(Colors::NEGATIVE, Colors::BG_BASE);
    tft.drawString("ERROR DE MEMORIA", SCREEN_W / 2, SCREEN_H / 2 - 14, &SatoshiBold24);
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    tft.drawString("REINICIA LA LEMON BOX", SCREEN_W / 2, SCREEN_H / 2 + 22, &Satoshi12);
}
}  // namespace

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
    drawHeader(data, device);
    switch (model.scene) {
        case USDT_OVERVIEW: drawOverview(data); break;
        case USDT_NETWORKS: drawNetworks(data); break;
        case USDT_MARKETS: drawMarkets(data); break;
        case USDT_REGIONS: drawRegions(data); break;
        case USDT_SYSTEM: drawSystem(data, device); break;
        default: drawOverview(data); break;
    }
    drawNavigation(model.scene);
    displayWaitVSync();
    s_canvas.pushSprite(0, 0);
}

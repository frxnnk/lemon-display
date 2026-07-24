#include "ui_v2_runtime.h"

#include "config.h"
#include "display_manager.h"
#include "data/PPNeueMachinaBold24.h"
#include "data/lemon_logo.h"
#include "data/lemon_v2_logo_light_120.h"
#include "data/satoshi_fonts.h"
#include "ui_v2_settings.h"
#include <Arduino.h>
#include <cmath>
#include <cstring>

static constexpr int V2_SAFE_INSET = 32;
static constexpr uint16_t V2_BLACK = 0x1082;
static constexpr uint16_t V2_MAIN_GREEN = 0x06E3;
static constexpr uint16_t V2_LIME_YELLOW = 0xCFE6;
static constexpr uint16_t V2_NEGATIVE = 0xF8E6;
static constexpr uint16_t V2_MUTED = 0x7BEF;
static constexpr int V2_HERO_LABEL_Y = 82;
static constexpr int V2_HERO_GRAPH_X = 252;
static constexpr int V2_HERO_GRAPH_Y = 106;
static constexpr int V2_HERO_GRAPH_W = 196;
static constexpr int V2_HERO_GRAPH_H = 76;
static constexpr int V2_HERO_PRICE_GRAPH_GAP = 12;
static constexpr int V2_HERO_PRICE_MAX_W = V2_HERO_GRAPH_X - V2_SAFE_INSET -
                                           V2_HERO_PRICE_GRAPH_GAP;
static constexpr int V2_HERO_CENTER_Y = 144;
static constexpr int V2_HERO_CHANGE_Y = 176;
static constexpr int V2_HERO_FRESHNESS_Y = 226;
static constexpr int V2_HOME_CARD_Y = 258;
static constexpr int V2_HOME_TAPE_Y = 370;
static constexpr int V2_HOME_TAPE_W = 340;
static constexpr int V2_SETTINGS_BUTTON_X = 390;
static constexpr int V2_SETTINGS_BUTTON_W = 58;
static constexpr int V2_PAIR_CLIP_X = 28;
static constexpr int V2_PAIR_CLIP_Y = 72;
static constexpr int V2_PAIR_CLIP_W = 424;
static constexpr int V2_PAIR_CLIP_H = 176;
static constexpr int V2_HOME_CARDS_CLIP_X = 28;
static constexpr int V2_HOME_CARDS_CLIP_Y = 254;
static constexpr int V2_HOME_CARDS_CLIP_W = 424;
static constexpr int V2_HOME_CARDS_CLIP_H = 80;
static constexpr int V2_LOADING_LOGO_W = 244;
static constexpr int V2_LOADING_LOGO_H = 56;
static constexpr int V2_LOADING_LOGO_X = (SCREEN_W - V2_LOADING_LOGO_W) / 2;
static constexpr int V2_LOADING_LOGO_Y = 190;
static LGFX_Sprite s_v2Sprite(&tft);
static bool s_spriteReady = false;
static bool s_loadingDrawn = false;
static char s_lastDrawnTime[12] = {};

static const char* freshnessText(V2Freshness freshness) {
    switch (freshness) {
        case V2_LOADING: return "CARGANDO";
        case V2_LIVE: return "LIVE";
        case V2_CACHED: return "CACHED";
        case V2_STALE: return "STALE";
        case V2_OFFLINE: return "OFFLINE";
        case V2_RATE_LIMITED: return "RATE LIMIT";
        default: return "ERROR";
    }
}

static V2Freshness stockFreshness(const StockFocusedSnapshot& stock,
                                  const V2RuntimeSnapshot& snapshot) {
    V2FetchStatus status = (strstr(stock.status, "429") || strstr(stock.status, "Reint"))
                         ? V2_FETCH_RATE_LIMITED
                         : (strstr(stock.status, "Error") || strstr(stock.status, "HTTP"))
                         ? V2_FETCH_NETWORK_ERROR
                         : V2_FETCH_OK;
    return v2Freshness(stock.hasQuote, stock.quote.lastUpdate, millis(),
                       snapshot.online, stock.fetching, status,
                       UPDATE_STOCKS_MS * 2UL, UPDATE_STOCKS_MS * 15UL);
}

static uint16_t trendColor(float change) {
    return change < 0.0f ? V2_NEGATIVE : V2_MAIN_GREEN;
}

static float pairTrend(const V2RuntimeSnapshot& snapshot, uint8_t pairIndex) {
    if (pairIndex == 0 && snapshot.btc.valid) return snapshot.btc.change24h;
    const SparklineData& spark = snapshot.pairSpark;
    if (spark.valid && spark.count >= 2 && isfinite(spark.points[0]) &&
        isfinite(spark.points[spark.count - 1])) {
        return spark.points[spark.count - 1] - spark.points[0];
    }
    return 0.0f;
}

static void drawLogo() {
    for (int y = 0; y < 28; y++) {
        for (int x = 0; x < 120; x++) {
            s_v2Sprite.drawPixel(V2_SAFE_INSET + x, V2_SAFE_INSET + y,
                                 pgm_read_word(&lemon_v2_logo_light_120[y * 120 + x]));
        }
    }
}

static uint16_t toGray565(uint16_t color) {
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    uint8_t gray5 = (r * 77 + (g >> 1) * 150 + b * 29) >> 8;
    uint8_t gray6 = gray5 << 1;
    return (gray5 << 11) | (gray6 << 5) | gray5;
}

static uint16_t mixColor565(uint16_t from, uint16_t to, uint8_t amount) {
    uint8_t r1 = (from >> 11) & 0x1F, g1 = (from >> 5) & 0x3F, b1 = from & 0x1F;
    uint8_t r2 = (to >> 11) & 0x1F, g2 = (to >> 5) & 0x3F, b2 = to & 0x1F;
    uint8_t r = r1 + ((static_cast<int>(r2) - r1) * amount) / 255;
    uint8_t g = g1 + ((static_cast<int>(g2) - g1) * amount) / 255;
    uint8_t b = b1 + ((static_cast<int>(b2) - b1) * amount) / 255;
    return (r << 11) | (g << 5) | b;
}

static void drawLoadingLogoProgress(uint8_t progress) {
    constexpr int transitionWidth = 18;
    int filled = (progress * V2_LOADING_LOGO_W) / 100;
    for (int y = 0; y < V2_LOADING_LOGO_H; y++) {
        for (int x = 0; x < V2_LOADING_LOGO_W; x++) {
            uint16_t color = pgm_read_word(&lemon_imagotipo_244[y * V2_LOADING_LOGO_W + x]);
            if (color == 0x0000) continue;
            uint8_t amount = 0;
            if (progress >= 100 || x <= filled - transitionWidth) {
                amount = 255;
            } else if (x < filled) {
                amount = static_cast<uint8_t>(((filled - x) * 255) / transitionWidth);
            }
            s_v2Sprite.drawPixel(V2_LOADING_LOGO_X + x, V2_LOADING_LOGO_Y + y,
                                 mixColor565(toGray565(color), color, amount));
        }
    }
}

static void pushClip(int x, int y, int w, int h) {
    displayWaitVSync();
    s_v2Sprite.setClipRect(x, y, w, h);
    tft.setClipRect(x, y, w, h);
    s_v2Sprite.pushSprite(0, 0);
    tft.clearClipRect();
    s_v2Sprite.clearClipRect();
}

static void drawChevron(int x, int y, bool right, uint16_t color) {
    int direction = right ? 1 : -1;
    s_v2Sprite.drawWideLine(x - direction * 5, y - 7, x + direction * 2, y, 2.5f, color);
    s_v2Sprite.drawWideLine(x + direction * 2, y, x - direction * 5, y + 7, 2.5f, color);
}

static void drawSettingsIcon(int x, int y, uint16_t color) {
    s_v2Sprite.drawWideLine(x - 10, y - 8, x + 10, y - 8, 2.0f, color);
    s_v2Sprite.drawWideLine(x - 10, y, x + 10, y, 2.0f, color);
    s_v2Sprite.drawWideLine(x - 10, y + 8, x + 10, y + 8, 2.0f, color);
    s_v2Sprite.fillCircle(x - 4, y - 8, 3, color);
    s_v2Sprite.fillCircle(x + 5, y, 3, color);
    s_v2Sprite.fillCircle(x - 1, y + 8, 3, color);
}

static void drawSettingsButton() {
    constexpr int centerX = V2_SETTINGS_BUTTON_X + V2_SETTINGS_BUTTON_W / 2;
    constexpr int centerY = V2_HOME_TAPE_Y + 29;
    s_v2Sprite.drawRoundRect(V2_SETTINGS_BUTTON_X, V2_HOME_TAPE_Y,
                             V2_SETTINGS_BUTTON_W, 58, 8, V2_MAIN_GREEN);
    drawSettingsIcon(centerX, centerY, V2_MAIN_GREEN);
}

static void formatHeroPrice(char* out, size_t outLen,
                            const PairDef& pair, float value) {
    const float magnitude = fabsf(value);
    float display = value;
    char scale = '\0';
    if (magnitude >= 1000000000.0f) {
        display = value / 1000000000.0f;
        scale = 'B';
    } else if (magnitude >= 1000000.0f) {
        display = value / 1000000.0f;
        scale = 'M';
    } else if (magnitude >= 100000.0f ||
               (strcmp(pair.label, "USD") != 0 && magnitude >= 1000.0f)) {
        display = value / 1000.0f;
        scale = 'K';
    }

    if (scale == 'K') {
        if (fabsf(display) < 100.0f) snprintf(out, outLen, "%s%.1fK", pair.prefix, display);
        else snprintf(out, outLen, "%s%.0fK", pair.prefix, display);
    } else if (scale == 'M') {
        if (fabsf(display) < 100.0f) snprintf(out, outLen, "%s%.1fM", pair.prefix, display);
        else snprintf(out, outLen, "%s%.0fM", pair.prefix, display);
    } else if (scale == 'B') {
        if (fabsf(display) < 100.0f) snprintf(out, outLen, "%s%.1fB", pair.prefix, display);
        else snprintf(out, outLen, "%s%.0fB", pair.prefix, display);
    } else if (pair.decimals == 0) {
        snprintf(out, outLen, "%s%.0f", pair.prefix, display);
    } else if (pair.decimals == 1) {
        snprintf(out, outLen, "%s%.1f", pair.prefix, display);
    } else {
        snprintf(out, outLen, "%s%.2f", pair.prefix, display);
    }
    for (char* c = out; *c; ++c) if (*c == '.') *c = ',';
}

static void drawPairSparkline(const SparklineData& spark) {
    constexpr int x = V2_HERO_GRAPH_X;
    constexpr int y = V2_HERO_GRAPH_Y;
    constexpr int w = V2_HERO_GRAPH_W;
    constexpr int h = V2_HERO_GRAPH_H;
    if (!spark.valid || spark.count < 2 || spark.maxVal <= spark.minVal) {
        s_v2Sprite.setTextColor(V2_MUTED, V2_BLACK);
        s_v2Sprite.setTextDatum(lgfx::middle_center);
        s_v2Sprite.drawString("SIN SERIE", x + w / 2, y + h / 2, &Satoshi9);
        return;
    }
    float range = spark.maxVal - spark.minVal;
    auto px = [&](int i) { return x + (i * (w - 1)) / (spark.count - 1); };
    auto py = [&](float value) {
        return y + h - 1 - static_cast<int>(((value - spark.minVal) / range) * (h - 2));
    };
    for (int i = 0; i < spark.count - 1; i++) {
        if (!isfinite(spark.points[i]) || !isfinite(spark.points[i + 1])) return;
        s_v2Sprite.drawWideLine(px(i), py(spark.points[i]),
                                px(i + 1), py(spark.points[i + 1]),
                                2.0f, V2_LIME_YELLOW);
    }
    s_v2Sprite.fillCircle(px(spark.count - 1), py(spark.points[spark.count - 1]), 3, V2_MAIN_GREEN);
}

static void drawHomeActions(const V2RuntimeSnapshot& snapshot) {
    s_v2Sprite.setTextDatum(lgfx::top_right);
    if (snapshot.otaAvailable) {
        s_v2Sprite.setTextColor(V2_LIME_YELLOW, V2_BLACK);
        if (snapshot.otaArmed) {
            s_v2Sprite.drawString("TOCA PARA CONFIRMAR", 448, V2_HERO_LABEL_Y, &Satoshi9);
        } else {
            char update[32];
            snprintf(update, sizeof(update), "ACTUALIZACION V%s", snapshot.otaVersion);
            s_v2Sprite.drawString(update, 448, V2_HERO_LABEL_Y, &Satoshi9);
        }
    } else {
        s_v2Sprite.setTextColor(V2_MUTED, V2_BLACK);
        s_v2Sprite.drawString("TOCA PARA CAMBIAR", 448, V2_HERO_LABEL_Y, &Satoshi9);
    }
}

static void drawHeader(const char* title, bool back, const V2RuntimeSnapshot& snapshot) {
    drawLogo();
    s_v2Sprite.setTextColor(V2_MAIN_GREEN, V2_BLACK);
    s_v2Sprite.setTextDatum(lgfx::top_right);
    s_v2Sprite.drawString(snapshot.time, 448, 36, &Satoshi12);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    if (back) {
        drawChevron(42, 88, false, V2_MAIN_GREEN);
        if (title && title[0]) s_v2Sprite.drawString(title, 62, 78, &SatoshiMedium18);
    } else {
        drawHomeActions(snapshot);
    }
}

static void drawPairPanel(const V2RuntimeSnapshot& snapshot) {
    uint8_t pairIndex = snapshot.selectedPair < BTC_PAIR_COUNT ? snapshot.selectedPair : 0;
    const PairDef& pair = BTC_PAIRS[pairIndex];
    s_v2Sprite.setTextDatum(lgfx::top_left);
    s_v2Sprite.setTextColor(V2_MAIN_GREEN, V2_BLACK);
    s_v2Sprite.drawString(pair.pairLabel, V2_SAFE_INSET, V2_HERO_LABEL_Y, &Satoshi9);

    char price[40] = "--";
    if (snapshot.pairValid) formatHeroPrice(price, sizeof(price), pair, snapshot.pairPrice);
    float trend = pairTrend(snapshot, pairIndex);
    s_v2Sprite.setTextDatum(lgfx::middle_left);
    s_v2Sprite.setTextColor(trendColor(trend), V2_BLACK);
    s_v2Sprite.setClipRect(V2_SAFE_INSET, V2_HERO_GRAPH_Y - 8,
                           V2_HERO_PRICE_MAX_W, V2_HERO_GRAPH_H + 16);
    s_v2Sprite.drawString(price, V2_SAFE_INSET, V2_HERO_CENTER_Y, &PPNeueMachinaBold24);
    s_v2Sprite.clearClipRect();
    drawPairSparkline(snapshot.pairSpark);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    if (pairIndex == 0 && snapshot.btc.valid) {
        char change[32];
        snprintf(change, sizeof(change), "%+.2f%% HOY", snapshot.btc.change24h);
        s_v2Sprite.setTextColor(trendColor(snapshot.btc.change24h), V2_BLACK);
        s_v2Sprite.drawString(change, V2_SAFE_INSET, V2_HERO_CHANGE_Y, &SatoshiMedium18);
    } else if (snapshot.pairFetching) {
        s_v2Sprite.setTextColor(V2_MUTED, V2_BLACK);
        s_v2Sprite.drawString("ACTUALIZANDO", V2_SAFE_INSET, V2_HERO_CHANGE_Y, &SatoshiMedium18);
    }
    s_v2Sprite.setTextColor(V2_MUTED, V2_BLACK);
    s_v2Sprite.setTextDatum(lgfx::top_right);
    s_v2Sprite.drawString(freshnessText(snapshot.pairFreshness), 448, V2_HERO_FRESHNESS_Y, &Satoshi9);
}

static void drawHomeCards(const V2RuntimeSnapshot& snapshot) {
    constexpr int cardY = V2_HOME_CARD_Y;
    constexpr int cardW = 200;
    constexpr int cardH = 72;
    constexpr int dollarX = 32;
    constexpr int stockX = 248;

    s_v2Sprite.drawRoundRect(dollarX, cardY, cardW, cardH, 8, V2_MAIN_GREEN);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    s_v2Sprite.setTextColor(V2_MAIN_GREEN, V2_BLACK);
    s_v2Sprite.drawString("DOLAR LEMON", dollarX + 10, cardY + 7, &Satoshi9);
    s_v2Sprite.setTextDatum(lgfx::top_right);
    s_v2Sprite.setTextColor(V2_MUTED, V2_BLACK);
    s_v2Sprite.drawString(freshnessText(snapshot.lemonFreshness), dollarX + cardW - 10, cardY + 7, &Satoshi9);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    s_v2Sprite.drawString("COMPRA", dollarX + 10, cardY + 30, &Satoshi9);
    s_v2Sprite.drawString("VENTA", dollarX + 108, cardY + 30, &Satoshi9);
    s_v2Sprite.setTextColor(V2_MAIN_GREEN, V2_BLACK);
    char bid[16] = "--";
    char ask[16] = "--";
    if (snapshot.lemon.valid) {
        snprintf(bid, sizeof(bid), "$%.0f", snapshot.lemon.bid);
        snprintf(ask, sizeof(ask), "$%.0f", snapshot.lemon.ask);
    }
    s_v2Sprite.drawString(bid, dollarX + 10, cardY + 47, &Satoshi12);
    s_v2Sprite.drawString(ask, dollarX + 108, cardY + 47, &Satoshi12);

    uint8_t focus = snapshot.stockCount ? snapshot.focusedStock % snapshot.stockCount : 0;
    const StockFocusedSnapshot* stock = snapshot.stockCount ? &snapshot.stocks[focus] : nullptr;
    s_v2Sprite.drawRoundRect(stockX, cardY, cardW, cardH, 8, V2_MAIN_GREEN);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    s_v2Sprite.setTextColor(V2_MAIN_GREEN, V2_BLACK);
    s_v2Sprite.drawString(stock ? stock->symbol : "WATCHLIST", stockX + 10, cardY + 9, &Satoshi12);
    if (stock && stock->hasQuote) {
        char stockValue[28];
        snprintf(stockValue, sizeof(stockValue), "$%.2f", stock->quote.price);
        s_v2Sprite.setTextColor(trendColor(stock->quote.changePct), V2_BLACK);
        s_v2Sprite.drawString(stockValue, stockX + 10, cardY + 36, &Satoshi12);
        char change[18];
        snprintf(change, sizeof(change), "%+.2f%%", stock->quote.changePct);
        s_v2Sprite.setTextDatum(lgfx::top_right);
        s_v2Sprite.drawString(change, stockX + cardW - 10, cardY + 36, &Satoshi12);
    } else {
        s_v2Sprite.setTextColor(V2_MUTED, V2_BLACK);
        s_v2Sprite.drawString(snapshot.stocksFetching ? "CARGANDO" : "SIN CACHE",
                              stockX + 10, cardY + 40, &Satoshi9);
    }
}

static void drawHome(const V2RuntimeSnapshot& snapshot) {
    drawHeader(nullptr, false, snapshot);
    drawPairPanel(snapshot);
    drawHomeCards(snapshot);

    s_v2Sprite.fillSmoothRoundRect(V2_SAFE_INSET, V2_HOME_TAPE_Y,
                                   V2_HOME_TAPE_W, 58, 8, V2_MAIN_GREEN);
    s_v2Sprite.setTextColor(V2_BLACK, V2_MAIN_GREEN);
    s_v2Sprite.setTextDatum(lgfx::middle_left);
    s_v2Sprite.drawString("MARKET TAPE", 52, V2_HOME_TAPE_Y + 29, &SatoshiMedium18);
    drawChevron(350, V2_HOME_TAPE_Y + 29, true, V2_BLACK);
    drawSettingsButton();
}

static void drawTape(const V2RuntimeSnapshot& snapshot) {
    drawHeader("MARKET TAPE", true, snapshot);
    for (uint8_t i = 0; i < V2_TAPE_ROWS; i++) {
        int y = 132 + i * 54;
        bool available = i < snapshot.stockCount;
        const StockFocusedSnapshot* stock = available ? &snapshot.stocks[i] : nullptr;
        s_v2Sprite.drawRoundRect(V2_SAFE_INSET, y, 416, 46, 7, available ? V2_LIME_YELLOW : V2_MUTED);
        s_v2Sprite.setTextColor(available ? V2_LIME_YELLOW : V2_MUTED, V2_BLACK);
        s_v2Sprite.setTextDatum(lgfx::middle_left);
        s_v2Sprite.drawString(available ? stock->symbol : "--", 48, y + 23, &Satoshi12);
        s_v2Sprite.setTextDatum(lgfx::middle_right);
        char value[48];
        if (stock && stock->hasQuote) {
            snprintf(value, sizeof(value), "$%.2f  %+.2f%%", stock->quote.price, stock->quote.changePct);
            s_v2Sprite.setTextColor(trendColor(stock->quote.changePct), V2_BLACK);
        } else {
            snprintf(value, sizeof(value), "%s", stock ? stock->status : "");
        }
        s_v2Sprite.drawString(value, 432, y + 23, &Satoshi9);
    }
    s_v2Sprite.setTextColor(V2_MUTED, V2_BLACK);
    s_v2Sprite.setTextDatum(lgfx::bottom_left);
    s_v2Sprite.drawString("YAHOO FINANCE / TOCA UNA FILA", V2_SAFE_INSET, 460, &Satoshi9);
}

static void drawContext(const V2RuntimeSnapshot& snapshot, uint8_t selected) {
    drawHeader("CONTEXTO", true, snapshot);
    const StockFocusedSnapshot* stock = selected < snapshot.stockCount ? &snapshot.stocks[selected] : nullptr;
    s_v2Sprite.setTextDatum(lgfx::top_left);
    s_v2Sprite.setTextColor(V2_LIME_YELLOW, V2_BLACK);
    s_v2Sprite.drawString(stock ? stock->symbol : "SIN DATO", V2_SAFE_INSET, 140, &PPNeueMachinaBold24);
    if (stock && stock->hasQuote) {
        s_v2Sprite.setTextColor(trendColor(stock->quote.changePct), V2_BLACK);
        char price[36];
        snprintf(price, sizeof(price), "$ %.2f", stock->quote.price);
        s_v2Sprite.drawString(price, V2_SAFE_INSET, 206, &PPNeueMachinaBold24);
        char move[36];
        snprintf(move, sizeof(move), "%+.2f%% HOY", stock->quote.changePct);
        s_v2Sprite.drawString(move, V2_SAFE_INSET, 270, &SatoshiMedium18);
        s_v2Sprite.setTextColor(V2_MAIN_GREEN, V2_BLACK);
        char range[64];
        snprintf(range, sizeof(range), "RANGO %.2f / %.2f", stock->quote.dayLow, stock->quote.dayHigh);
        s_v2Sprite.drawString(range, V2_SAFE_INSET, 318, &Satoshi12);
        s_v2Sprite.setTextColor(V2_MUTED, V2_BLACK);
        s_v2Sprite.drawString(freshnessText(stockFreshness(*stock, snapshot)), V2_SAFE_INSET, 352, &Satoshi9);
    } else {
        s_v2Sprite.drawString("CARGANDO DATOS REALES", V2_SAFE_INSET, 228, &Satoshi12);
    }
    s_v2Sprite.drawFastHLine(V2_SAFE_INSET, 390, 416, V2_MUTED);
    s_v2Sprite.setTextColor(V2_MUTED, V2_BLACK);
    s_v2Sprite.drawString("FUENTE: YAHOO FINANCE", V2_SAFE_INSET, 410, &Satoshi9);
    s_v2Sprite.drawString("PROVIDER PENDIENTE PARA NOTICIAS", V2_SAFE_INSET, 434, &Satoshi9);
}

static void drawWifiRecovery(const V2RuntimeSnapshot& snapshot) {
    drawLogo();
    s_v2Sprite.setTextDatum(lgfx::top_left);
    s_v2Sprite.setTextColor(V2_LIME_YELLOW, V2_BLACK);
    s_v2Sprite.drawString("SIN CONEXION", V2_SAFE_INSET, 132, &PPNeueMachinaBold24);
    s_v2Sprite.setTextColor(V2_MAIN_GREEN, V2_BLACK);
    s_v2Sprite.drawString("No pudimos conectar a", V2_SAFE_INSET, 208, &Satoshi12);
    s_v2Sprite.setTextColor(V2_LIME_YELLOW, V2_BLACK);
    s_v2Sprite.drawString(snapshot.ssid[0] ? snapshot.ssid : "la red guardada",
                          V2_SAFE_INSET, 240, &SatoshiMedium18);
    s_v2Sprite.setTextColor(V2_MUTED, V2_BLACK);
    s_v2Sprite.drawString("Seguimos reintentando en segundo plano.",
                          V2_SAFE_INSET, 280, &Satoshi9);
    s_v2Sprite.fillSmoothRoundRect(V2_SAFE_INSET, 330, 416, 66, 8, V2_MAIN_GREEN);
    s_v2Sprite.setTextColor(V2_BLACK, V2_MAIN_GREEN);
    s_v2Sprite.setTextDatum(lgfx::middle_left);
    s_v2Sprite.drawString("CONFIGURAR OTRA RED", 52, 363, &SatoshiMedium18);
    drawChevron(426, 363, true, V2_BLACK);
    s_v2Sprite.setTextColor(V2_MUTED, V2_BLACK);
    s_v2Sprite.setTextDatum(lgfx::bottom_left);
    s_v2Sprite.drawString("TOCA PARA ABRIR EL QR", V2_SAFE_INSET, 454, &Satoshi9);
}

void v2UiSetup() {
    if (s_spriteReady) return;
    s_v2Sprite.setPsram(true);
    s_v2Sprite.setColorDepth(16);
    s_v2Sprite.createSprite(SCREEN_W, SCREEN_H);
    s_spriteReady = true;
}

void v2UiDrawLoading(const char* status, uint8_t progress) {
    v2UiSetup();
    if (progress > 100) progress = 100;
    if (!s_loadingDrawn) {
        s_v2Sprite.fillSprite(V2_BLACK);
        s_loadingDrawn = true;
    }
    s_v2Sprite.fillRect(V2_LOADING_LOGO_X, V2_LOADING_LOGO_Y,
                        V2_LOADING_LOGO_W, V2_LOADING_LOGO_H, V2_BLACK);
    drawLoadingLogoProgress(progress);
    s_v2Sprite.fillRect(40, 270, 400, 42, V2_BLACK);
    s_v2Sprite.setTextColor(progress == 100 ? V2_LIME_YELLOW : V2_MAIN_GREEN, V2_BLACK);
    s_v2Sprite.setTextDatum(lgfx::middle_center);
    s_v2Sprite.drawString(status, 240, 291, &Satoshi12);
    if (progress <= 5) {
        displayWaitVSync();
        s_v2Sprite.pushSprite(0, 0);
    } else {
        pushClip(100, 180, 280, 138);
    }
}

void v2UiDraw(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    v2UiSetup();
    s_v2Sprite.fillSprite(V2_BLACK);
    if (model.scene == V2_HOME) drawHome(snapshot);
    else if (model.scene == V2_MARKET_TAPE) drawTape(snapshot);
    else if (model.scene == V2_CONTEXT) drawContext(snapshot, model.selectedStock);
    else if (model.scene == V2_SETTINGS) v2DrawSettings(s_v2Sprite, snapshot, model);
    else drawWifiRecovery(snapshot);
    displayWaitVSync();
    s_v2Sprite.pushSprite(0, 0);
    strncpy(s_lastDrawnTime, snapshot.time, sizeof(s_lastDrawnTime) - 1);
    s_lastDrawnTime[sizeof(s_lastDrawnTime) - 1] = '\0';
    s_loadingDrawn = false;
}

void v2UiUpdateClock(const char* time, V2Scene scene) {
    if (!s_spriteReady || scene == V2_SETTINGS) return;
    if (strcmp(time, s_lastDrawnTime) == 0) return;
    constexpr int x = 344;
    constexpr int y = 30;
    constexpr int w = 108;
    constexpr int h = 30;
    s_v2Sprite.fillRect(x, y, w, h, V2_BLACK);
    s_v2Sprite.setTextColor(V2_MAIN_GREEN, V2_BLACK);
    s_v2Sprite.setTextDatum(lgfx::top_right);
    s_v2Sprite.drawString(time, 448, 36, &Satoshi12);
    pushClip(x, y, w, h);
    strncpy(s_lastDrawnTime, time, sizeof(s_lastDrawnTime) - 1);
    s_lastDrawnTime[sizeof(s_lastDrawnTime) - 1] = '\0';
}

void v2UiUpdateStatus(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_HOME) return;
    constexpr int x = 28, y = 70, w = 424, h = 48;
    s_v2Sprite.fillRect(x, y, w, h, V2_BLACK);
    drawHomeActions(snapshot);
    pushClip(x, y, w, h);
}

void v2UiUpdatePair(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_HOME) return;
    s_v2Sprite.fillRect(V2_PAIR_CLIP_X, V2_PAIR_CLIP_Y, V2_PAIR_CLIP_W, V2_PAIR_CLIP_H, V2_BLACK);
    drawPairPanel(snapshot);
    pushClip(V2_PAIR_CLIP_X, V2_PAIR_CLIP_Y, V2_PAIR_CLIP_W, V2_PAIR_CLIP_H);
}

void v2UiUpdateHomeCards(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_HOME) return;
    s_v2Sprite.fillRect(V2_HOME_CARDS_CLIP_X, V2_HOME_CARDS_CLIP_Y,
                        V2_HOME_CARDS_CLIP_W, V2_HOME_CARDS_CLIP_H, V2_BLACK);
    drawHomeCards(snapshot);
    pushClip(V2_HOME_CARDS_CLIP_X, V2_HOME_CARDS_CLIP_Y,
             V2_HOME_CARDS_CLIP_W, V2_HOME_CARDS_CLIP_H);
}

void v2UiUpdateTape(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_MARKET_TAPE) return;
    constexpr int x = 28, y = 124, w = 424, h = 348;
    s_v2Sprite.fillRect(x, y, w, h, V2_BLACK);
    drawTape(snapshot);
    pushClip(x, y, w, h);
}

void v2UiUpdateContext(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_CONTEXT) return;
    constexpr int x = 28, y = 126, w = 424, h = 342;
    s_v2Sprite.fillRect(x, y, w, h, V2_BLACK);
    drawContext(snapshot, model.selectedStock);
    pushClip(x, y, w, h);
}

void v2UiUpdateData(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (model.scene == V2_HOME) {
        constexpr int y = V2_PAIR_CLIP_Y;
        constexpr int h = V2_HOME_CARDS_CLIP_Y + V2_HOME_CARDS_CLIP_H - y;
        s_v2Sprite.fillRect(V2_PAIR_CLIP_X, y, V2_PAIR_CLIP_W, h, V2_BLACK);
        drawPairPanel(snapshot);
        drawHomeCards(snapshot);
        pushClip(V2_PAIR_CLIP_X, y, V2_PAIR_CLIP_W, h);
    } else if (model.scene == V2_MARKET_TAPE) {
        v2UiUpdateTape(snapshot, model);
    } else if (model.scene == V2_CONTEXT) {
        v2UiUpdateContext(snapshot, model);
    }
}

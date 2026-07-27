#include "ui_v2_runtime.h"

#include "config.h"
#include "display_manager.h"
#include "nvs_storage.h"
#include "data/PPNeueMachinaBold24.h"
#include "data/btc_token_32.h"
#include "data/lemon_logo.h"
#include "data/lemon_v2_logo_light_120.h"
#include "data/lemon_v2_logo_black_120.h"
#include "data/satoshi_fonts.h"
#include "data/usdc_token_32.h"
#include "ui_v2_settings.h"
#include <Arduino.h>
#include <cmath>
#include <cstring>

static constexpr int V2_SAFE_INSET = 32;
static constexpr int V2_HERO_LABEL_Y = 82;
static constexpr int V2_HERO_CENTER_Y = 132;
static constexpr int V2_HERO_CHANGE_Y = 168;
static constexpr int V2_HERO_GRAPH_X = 252;
static constexpr int V2_HERO_GRAPH_Y = 98;
static constexpr int V2_HERO_GRAPH_W = 196;
static constexpr int V2_HERO_GRAPH_H = 76;
static constexpr int V2_HERO_PRICE_GRAPH_GAP = 12;
static constexpr int V2_HERO_PRICE_MAX_W = V2_HERO_GRAPH_X - V2_SAFE_INSET -
                                           V2_HERO_PRICE_GRAPH_GAP;
static constexpr int V2_NEWS_Y = 228;
static constexpr int V2_NEWS_H = 72;
static constexpr int V2_HOME_CARD_Y = 328;
static constexpr int V2_HERO_CLIP_X = 28;
static constexpr int V2_HERO_CLIP_Y = 72;
static constexpr int V2_HERO_CLIP_W = 424;
static constexpr int V2_HERO_CLIP_H = 156;
static constexpr int V2_HERO_TEXT_CLIP_X = 28;
static constexpr int V2_HERO_TEXT_CLIP_Y = 72;
static constexpr int V2_HERO_TEXT_CLIP_W = 212;
static constexpr int V2_HERO_TEXT_CLIP_H = 128;
static constexpr int V2_HERO_GRAPH_CLIP_X = 248;
static constexpr int V2_HERO_GRAPH_CLIP_Y = 94;
static constexpr int V2_HERO_GRAPH_CLIP_W = 204;
static constexpr int V2_HERO_GRAPH_CLIP_H = 88;
static constexpr int V2_NEWS_CLIP_X = 28;
static constexpr int V2_NEWS_CLIP_Y = 228;
static constexpr int V2_NEWS_CLIP_W = 424;
static constexpr int V2_NEWS_CLIP_H = 72;
static constexpr int V2_CARDS_CLIP_X = 28;
static constexpr int V2_CARDS_CLIP_Y = 324;
static constexpr int V2_CARDS_CLIP_W = 424;
static constexpr int V2_CARDS_CLIP_H = 128;
static constexpr int V2_CARD_Y = V2_HOME_CARD_Y;
static constexpr int V2_CARD_W = 200;
static constexpr int V2_CARD_H = 120;
static constexpr int V2_BTC_X = 32;
static constexpr int V2_DOLLAR_X = 248;
static constexpr int V2_LOADING_LOGO_W = 244;
static constexpr int V2_LOADING_LOGO_H = 56;
static constexpr int V2_LOADING_LOGO_X = (SCREEN_W - V2_LOADING_LOGO_W) / 2;
static constexpr int V2_LOADING_LOGO_Y = 190;

struct V2Palette {
    uint16_t bg, text, accent, highlight, muted, neg, pos, labelBg, labelFg;
};
static const V2Palette s_darkPal = {
    0x1082, 0xFFFF, 0x06E3, 0xCFE6, 0x7BEF, 0xF8E6, 0x06E3, 0x06E3, 0x1082
};
static const V2Palette s_lightPal = {
    0xCFE6, 0x1082, 0x1082, 0x1082, 0x01E3, 0x1082, 0x01E3, 0x06E3, 0x1082
};
static const V2Palette* pal = &s_darkPal;
static bool s_isLight = false;

void v2UiSetTheme(uint8_t theme) {
    s_isLight = theme == 1;
    pal = s_isLight ? &s_lightPal : &s_darkPal;
}

bool v2UiIsLightTheme() { return s_isLight; }
uint16_t v2UiColorBg() { return pal->bg; }
uint16_t v2UiColorText() { return pal->text; }
uint16_t v2UiColorAccent() { return pal->accent; }
uint16_t v2UiColorHighlight() { return pal->highlight; }
uint16_t v2UiColorMuted() { return pal->muted; }
uint16_t v2UiColorLabelBg() { return pal->labelBg; }
uint16_t v2UiColorLabelFg() { return pal->labelFg; }

static LGFX_Sprite s_v2Sprite(&tft);
static bool s_spriteReady = false;
static bool s_loadingDrawn = false;
static char s_lastDrawnTime[12] = {};

static uint16_t trendColor(float change) {
    return change < 0.0f ? pal->neg : pal->pos;
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

static const char* freshnessLabel(V2Freshness freshness) {
    switch (freshness) {
        case V2_LOADING: return "CARGA";
        case V2_LIVE: return "LIVE";
        case V2_CACHED: return "CACHE";
        case V2_STALE: return "STALE";
        case V2_OFFLINE: return "OFFLINE";
        case V2_RATE_LIMITED: return "LIMITE";
        case V2_ERROR: return "ERROR";
    }
    return "ERROR";
}

static uint16_t freshnessColor(V2Freshness freshness) {
    return freshness == V2_LIVE ? pal->accent
         : freshness == V2_ERROR || freshness == V2_RATE_LIMITED
             ? pal->neg
         : pal->muted;
}

static void drawTokenIcon(const uint16_t* pixels, int x, int y) {
    for (int py = 0; py < 32; py++) {
        for (int px = 0; px < 32; px++) {
            const uint16_t color = pgm_read_word(&pixels[py * 32 + px]);
            if (color != 0x0000) s_v2Sprite.drawPixel(x + px, y + py, color);
        }
    }
}

static void drawLogo() {
    const uint16_t* logo = s_isLight
        ? lemon_v2_logo_black_120
        : lemon_v2_logo_light_120;
    for (int y = 0; y < 28; y++) {
        for (int x = 0; x < 120; x++) {
            s_v2Sprite.drawPixel(V2_SAFE_INSET + x, V2_SAFE_INSET + y,
                                 pgm_read_word(&logo[y * 120 + x]));
        }
    }
}

static void drawSparkline(const SparklineData& spark, int x, int y, int w,
                          int h, uint16_t color, bool showEmptyState = true) {
    if (!spark.valid || spark.count < 2 || spark.maxVal <= spark.minVal) {
        if (showEmptyState) {
            s_v2Sprite.setTextColor(pal->muted, pal->bg);
            s_v2Sprite.setTextDatum(lgfx::middle_center);
            s_v2Sprite.drawString("SIN SERIE", x + w / 2, y + h / 2, &Satoshi9);
        }
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
                                2.0f, color);
    }
    s_v2Sprite.fillCircle(px(spark.count - 1), py(spark.points[spark.count - 1]), 3, pal->accent);
}

static void drawCompactSparkline(const SparklineData& spark, int x, int y,
                                 int w, int h, uint16_t color) {
    drawSparkline(spark, x, y, w, h, color, false);
}

struct DeferredClip { int16_t x, y, w, h; bool active; };
static constexpr uint8_t V2_MAX_DEFERRED_CLIPS = 8;
static DeferredClip s_defClips[V2_MAX_DEFERRED_CLIPS] = {};
static bool s_deferPush = false;
static void clearDeferredClips() {
    for (auto& clip : s_defClips) clip.active = false;
}
static DeferredClip* nextDeferredClip() {
    for (auto& clip : s_defClips) {
        if (clip.active) return &clip;
    }
    return nullptr;
}
static bool clipsOverlap(const DeferredClip& a, const DeferredClip& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}
static void mergeClip(DeferredClip& dst, const DeferredClip& src) {
    int16_t x = min(dst.x, src.x);
    int16_t y = min(dst.y, src.y);
    int16_t right = max(dst.x + dst.w, src.x + src.w);
    int16_t bottom = max(dst.y + dst.h, src.y + src.h);
    dst = {x, y, static_cast<int16_t>(right - x),
           static_cast<int16_t>(bottom - y), true};
}
static void queueDefClip(int16_t x, int16_t y, int16_t w, int16_t h) {
    DeferredClip incoming = {x, y, w, h, true};
    for (auto& clip : s_defClips) {
        if (clip.active && clipsOverlap(clip, incoming)) {
            mergeClip(clip, incoming);
            return;
        }
    }
    for (auto& clip : s_defClips) {
        if (!clip.active) {
            clip = incoming;
            return;
        }
    }

    // Eight independent redraws cannot occur in one runtime tick today.
    // If that changes, preserve correctness by coalescing only the final slot.
    mergeClip(s_defClips[V2_MAX_DEFERRED_CLIPS - 1], incoming);
}
static void pushClip(int x, int y, int w, int h) {
    if (s_deferPush) { queueDefClip(x, y, w, h); return; }
    displayWaitVSync();
    s_v2Sprite.setClipRect(x, y, w, h);
    tft.setClipRect(x, y, w, h);
    uint32_t pushStartedUs = micros();
    s_v2Sprite.pushSprite(0, 0);
    displayRecordPush(static_cast<uint32_t>(w * h * 2),
                      micros() - pushStartedUs);
    tft.clearClipRect();
    s_v2Sprite.clearClipRect();
}
void v2UiSetDeferred(bool defer) {
    s_deferPush = defer;
}
void v2UiFlushDeferred() {
    if (!s_deferPush) return;
    s_deferPush = false;
    DeferredClip* clip = nextDeferredClip();
    if (!clip) return;
    displayWaitVSync();
    s_v2Sprite.setClipRect(clip->x, clip->y, clip->w, clip->h);
    tft.setClipRect(clip->x, clip->y, clip->w, clip->h);
    uint32_t pushStartedUs = micros();
    s_v2Sprite.pushSprite(0, 0);
    displayRecordPush(static_cast<uint32_t>(clip->w * clip->h * 2),
                      micros() - pushStartedUs);
    clip->active = false;
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

static void drawUpdateBadge() {
    constexpr int x = 338;
    constexpr int y = 26;
    constexpr int w = 62;
    constexpr int h = 22;
    s_v2Sprite.fillSmoothRoundRect(x, y, w, h, h / 2, pal->highlight);
    s_v2Sprite.setTextDatum(lgfx::middle_center);
    s_v2Sprite.setTextColor(pal->bg, pal->highlight);
    s_v2Sprite.drawString("UPDATE", x + w / 2, y + h / 2, &Satoshi9);
}

static uint16_t mixColor565(uint16_t from, uint16_t to, uint8_t amount) {
    uint8_t r1 = (from >> 11) & 0x1F, g1 = (from >> 5) & 0x3F, b1 = from & 0x1F;
    uint8_t r2 = (to >> 11) & 0x1F, g2 = (to >> 5) & 0x3F, b2 = to & 0x1F;
    uint8_t r = r1 + ((static_cast<int>(r2) - r1) * amount) / 255;
    uint8_t g = g1 + ((static_cast<int>(g2) - g1) * amount) / 255;
    uint8_t b = b1 + ((static_cast<int>(b2) - b1) * amount) / 255;
    return (r << 11) | (g << 5) | b;
}

static uint16_t cardFillColor() {
    return mixColor565(pal->bg, pal->labelBg, 35);
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

static void formatArsPrice(char* out, size_t outLen, float value) {
    char digits[16];
    snprintf(digits, sizeof(digits), "%.0f", value);
    const size_t digitsLen = strlen(digits);
    size_t dst = 0;
    if (outLen > 1) out[dst++] = '$';
    for (size_t i = 0; i < digitsLen && dst + 1 < outLen; i++) {
        const size_t remaining = digitsLen - i;
        if (i > 0 && remaining % 3 == 0 && dst + 2 < outLen) {
            out[dst++] = '.';
        }
        out[dst++] = digits[i];
    }
    out[dst] = '\0';
}

static bool cardPriceFont(const char* price) {
    return s_v2Sprite.textWidth(price, &PPNeueMachinaBold24) <=
           V2_CARD_W - 20;
}

static void drawHeader(bool back, const char* title, const V2RuntimeSnapshot& snapshot) {
    drawLogo();
    s_v2Sprite.setTextDatum(lgfx::top_center);
    s_v2Sprite.setTextColor(pal->accent, pal->bg);
    s_v2Sprite.drawString(snapshot.time, SCREEN_W / 2, 32, &SatoshiMedium18);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    if (back) {
        drawChevron(42, 88, false, pal->accent);
        if (title && title[0]) {
            s_v2Sprite.setTextColor(pal->text, pal->bg);
            s_v2Sprite.drawString(title, 62, 78, &SatoshiMedium18);
        }
    } else {
        if (snapshot.otaAvailable) drawUpdateBadge();
        drawSettingsIcon(424, 36, pal->accent);
    }
}

static void drawStockHero(const V2RuntimeSnapshot& snapshot) {
    uint8_t focus = snapshot.stockCount ? snapshot.focusedStock % snapshot.stockCount : 0;
    const StockFocusedSnapshot* stock = snapshot.stockCount ? &snapshot.stocks[focus] : nullptr;
    s_v2Sprite.setTextDatum(lgfx::top_left);
    if (stock) {
        s_v2Sprite.setTextColor(pal->highlight, pal->bg);
        s_v2Sprite.drawString(stock->symbol, V2_SAFE_INSET, V2_HERO_LABEL_Y, &SatoshiMedium18);
    }
    char price[40] = "--";
    if (stock && stock->hasQuote) {
        snprintf(price, sizeof(price), "$%.0f", stock->quote.price);
    }
    s_v2Sprite.setTextDatum(lgfx::middle_left);
    s_v2Sprite.setTextColor(stock && stock->hasQuote ? trendColor(stock->quote.changePct) : pal->muted,
                            pal->bg);
    s_v2Sprite.drawString(price, V2_SAFE_INSET, V2_HERO_CENTER_Y, &PPNeueMachinaBold24);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    if (stock && stock->hasQuote) {
        char change[32];
        snprintf(change, sizeof(change), "%+.2f%% HOY", stock->quote.changePct);
        s_v2Sprite.setTextColor(trendColor(stock->quote.changePct), pal->bg);
        s_v2Sprite.drawString(change, V2_SAFE_INSET, V2_HERO_CHANGE_Y, &SatoshiMedium18);
    } else if (snapshot.stocksFetching) {
        s_v2Sprite.setTextColor(pal->muted, pal->bg);
        s_v2Sprite.drawString("CARGANDO", V2_SAFE_INSET, V2_HERO_CHANGE_Y, &SatoshiMedium18);
    }
    if (stock && stock->hasQuote && stock->hasSpark) {
        drawSparkline(stock->spark, V2_HERO_GRAPH_X, V2_HERO_GRAPH_Y,
                      V2_HERO_GRAPH_W, V2_HERO_GRAPH_H, pal->highlight);
    }
}

static constexpr uint8_t NEWS_READER_MAX_LINES = 6;

struct WrappedHeadline {
    char lines[NEWS_READER_MAX_LINES][NEWS_TITLE_LEN];
    uint8_t count;
    bool truncated;
};

static bool appendHeadlineWord(char* line, const char* word,
                               size_t wordLen, int maxWidth) {
    const size_t lineLen = strlen(line);
    const size_t separatorLen = lineLen > 0 ? 1 : 0;
    if (lineLen + separatorLen + wordLen >= NEWS_TITLE_LEN) return false;

    char candidate[NEWS_TITLE_LEN];
    memcpy(candidate, line, lineLen);
    size_t next = lineLen;
    if (separatorLen) candidate[next++] = ' ';
    memcpy(candidate + next, word, wordLen);
    candidate[next + wordLen] = '\0';
    if (s_v2Sprite.textWidth(candidate, &SatoshiMedium18) > maxWidth) return false;

    memcpy(line, candidate, next + wordLen + 1);
    return true;
}

static void addHeadlineEllipsis(char* line, int maxWidth) {
    while (line[0]) {
        char candidate[NEWS_TITLE_LEN];
        snprintf(candidate, sizeof(candidate), "%s...", line);
        if (s_v2Sprite.textWidth(candidate, &SatoshiMedium18) <= maxWidth) {
            strncpy(line, candidate, NEWS_TITLE_LEN - 1);
            line[NEWS_TITLE_LEN - 1] = '\0';
            return;
        }
        char* lastSpace = strrchr(line, ' ');
        if (!lastSpace) {
            line[0] = '\0';
            break;
        }
        *lastSpace = '\0';
    }
    strncpy(line, "...", NEWS_TITLE_LEN);
}

static WrappedHeadline wrapHeadline(const char* title, int maxWidth,
                                    uint8_t maxLines) {
    WrappedHeadline layout = {};
    if (!title || !title[0] || maxLines == 0) return layout;
    if (maxLines > NEWS_READER_MAX_LINES) maxLines = NEWS_READER_MAX_LINES;

    uint8_t lineIndex = 0;
    layout.count = 1;
    const char* cursor = title;
    while (*cursor) {
        while (*cursor == ' ') cursor++;
        if (!*cursor) break;

        const char* word = cursor;
        while (*cursor && *cursor != ' ') cursor++;
        const size_t wordLen = static_cast<size_t>(cursor - word);
        if (appendHeadlineWord(layout.lines[lineIndex], word, wordLen,
                               maxWidth)) {
            continue;
        }

        if (lineIndex + 1 < maxLines) {
            lineIndex++;
            layout.count = lineIndex + 1;
            if (appendHeadlineWord(layout.lines[lineIndex], word, wordLen,
                                   maxWidth)) {
                continue;
            }
        }
        layout.truncated = true;
        break;
    }

    if (layout.truncated) {
        addHeadlineEllipsis(layout.lines[lineIndex], maxWidth);
    }
    return layout;
}

static WrappedHeadline layoutHeadline(const char* title, int maxWidth) {
    return wrapHeadline(title, maxWidth, 2);
}

static void drawHeadlineBlock(const StockNews& news, int x, int y, int maxWidth) {
    const WrappedHeadline layout = layoutHeadline(news.title, maxWidth);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    s_v2Sprite.setTextColor(pal->text, pal->bg);
    s_v2Sprite.drawString(layout.lines[0], x, y, &SatoshiMedium18);
    s_v2Sprite.drawString(layout.lines[1], x, y + 26, &SatoshiMedium18);
    if (news.pubDate[0]) {
        s_v2Sprite.setTextColor(pal->muted, pal->bg);
        s_v2Sprite.drawString(news.pubDate, x, y + 54, &Satoshi9);
    }
}

static void drawNewsHeadline(const V2RuntimeSnapshot& snapshot) {
    s_v2Sprite.fillRect(V2_NEWS_CLIP_X, V2_NEWS_CLIP_Y, V2_NEWS_CLIP_W, V2_NEWS_CLIP_H, pal->bg);
    s_v2Sprite.setClipRect(V2_NEWS_CLIP_X, V2_NEWS_CLIP_Y, V2_NEWS_CLIP_W, V2_NEWS_CLIP_H);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    if (snapshot.newsCount > 0 && snapshot.news[0].valid && snapshot.news[0].title[0]) {
        drawHeadlineBlock(snapshot.news[0], V2_SAFE_INSET, V2_NEWS_Y, 416);
    } else if (snapshot.newsFetching) {
        s_v2Sprite.setTextColor(pal->muted, pal->bg);
        s_v2Sprite.drawString("CARGANDO NOTICIAS...", V2_SAFE_INSET, V2_NEWS_Y + 8, &Satoshi9);
    } else {
        s_v2Sprite.setTextColor(pal->muted, pal->bg);
        s_v2Sprite.drawString("SIN NOTICIAS / TOCA PARA REINTENTAR", V2_SAFE_INSET, V2_NEWS_Y + 8, &Satoshi9);
    }
    s_v2Sprite.clearClipRect();
}

static void drawBtcCard(const V2RuntimeSnapshot& snapshot) {
    uint8_t pairIndex = snapshot.selectedPair < BTC_PAIR_COUNT ? snapshot.selectedPair : 0;
    const PairDef& pair = BTC_PAIRS[pairIndex];
    const uint16_t cardBg = cardFillColor();
    s_v2Sprite.fillSmoothRoundRect(V2_BTC_X, V2_CARD_Y, V2_CARD_W,
                                   V2_CARD_H, 8, cardBg);
    s_v2Sprite.drawRoundRect(V2_BTC_X, V2_CARD_Y, V2_CARD_W, V2_CARD_H, 8, pal->accent);
    drawTokenIcon(btc_token_32, V2_BTC_X + 10, V2_CARD_Y + 7);
    if (pairIndex > 0) {
        char pairLabel[16];
        snprintf(pairLabel, sizeof(pairLabel), "VS %s", pair.label);
        s_v2Sprite.setTextDatum(lgfx::top_left);
        s_v2Sprite.setTextColor(pal->muted, cardBg);
        s_v2Sprite.drawString(pairLabel, V2_BTC_X + 50,
                              V2_CARD_Y + 14, &Satoshi12);
    }

    s_v2Sprite.setTextDatum(lgfx::top_right);
    if (pairIndex == 0 && snapshot.btc.valid) {
        char change[24];
        snprintf(change, sizeof(change), "%+.1f%%", snapshot.btc.change24h);
        s_v2Sprite.setTextColor(trendColor(snapshot.btc.change24h), cardBg);
        s_v2Sprite.drawString(change, V2_BTC_X + V2_CARD_W - 10,
                              V2_CARD_Y + 3, &SatoshiMedium18);
        s_v2Sprite.setTextColor(pal->muted, cardBg);
        s_v2Sprite.drawString("24H", V2_BTC_X + V2_CARD_W - 10,
                              V2_CARD_Y + 27, &Satoshi9);
    } else {
        s_v2Sprite.setTextColor(freshnessColor(snapshot.pairFreshness), cardBg);
        s_v2Sprite.drawString(freshnessLabel(snapshot.pairFreshness),
                              V2_BTC_X + V2_CARD_W - 10,
                              V2_CARD_Y + 14, &Satoshi9);
    }

    char price[28] = "--";
    if (snapshot.pairValid) formatHeroPrice(price, sizeof(price), pair, snapshot.pairPrice);
    s_v2Sprite.setTextColor(snapshot.pairValid ? trendColor(pairTrend(snapshot, pairIndex)) : pal->muted,
                            cardBg);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    if (cardPriceFont(price)) {
        s_v2Sprite.drawString(price, V2_BTC_X + 10, V2_CARD_Y + 39,
                              &PPNeueMachinaBold24);
    } else {
        s_v2Sprite.drawString(price, V2_BTC_X + 10, V2_CARD_Y + 49,
                              &SatoshiMedium18);
    }
    drawCompactSparkline(snapshot.pairSpark, V2_BTC_X + 10,
                         V2_CARD_Y + 88, V2_CARD_W - 20, 22,
                         snapshot.pairValid
                             ? trendColor(pairTrend(snapshot, pairIndex))
                             : pal->muted);
}

static void drawDollarCard(const V2RuntimeSnapshot& snapshot) {
    const uint16_t cardBg = cardFillColor();
    s_v2Sprite.fillSmoothRoundRect(V2_DOLLAR_X, V2_CARD_Y, V2_CARD_W,
                                   V2_CARD_H, 8, cardBg);
    s_v2Sprite.drawRoundRect(V2_DOLLAR_X, V2_CARD_Y, V2_CARD_W, V2_CARD_H, 8, pal->accent);
    drawTokenIcon(usdc_token_32, V2_DOLLAR_X + 10, V2_CARD_Y + 7);
    char change[20] = "--";
    if (snapshot.lemonChange24hValid) {
        snprintf(change, sizeof(change), "%+.1f%%", snapshot.lemonChange24h);
    }
    s_v2Sprite.setTextDatum(lgfx::top_right);
    s_v2Sprite.setTextColor(snapshot.lemonChange24hValid
                                ? trendColor(snapshot.lemonChange24h)
                                : pal->muted,
                            cardBg);
    s_v2Sprite.drawString(change, V2_DOLLAR_X + V2_CARD_W - 10,
                          V2_CARD_Y + 3, &SatoshiMedium18);
    s_v2Sprite.setTextColor(pal->muted, cardBg);
    s_v2Sprite.drawString("24H", V2_DOLLAR_X + V2_CARD_W - 10,
                          V2_CARD_Y + 27, &Satoshi9);

    char bid[16] = "--";
    char ask[16] = "--";
    if (snapshot.lemon.valid) {
        formatArsPrice(bid, sizeof(bid), snapshot.lemon.bid);
        formatArsPrice(ask, sizeof(ask), snapshot.lemon.ask);
    }
    s_v2Sprite.setTextDatum(lgfx::top_left);
    s_v2Sprite.setTextColor(pal->muted, cardBg);
    s_v2Sprite.drawString("COMPRA", V2_DOLLAR_X + 10,
                          V2_CARD_Y + 53, &Satoshi9);
    s_v2Sprite.drawString("VENTA", V2_DOLLAR_X + 10,
                          V2_CARD_Y + 82, &Satoshi9);
    s_v2Sprite.setTextDatum(lgfx::top_right);
    s_v2Sprite.setTextColor(pal->text, cardBg);
    s_v2Sprite.drawString(bid, V2_DOLLAR_X + V2_CARD_W - 10,
                          V2_CARD_Y + 45, &SatoshiMedium18);
    s_v2Sprite.drawString(ask, V2_DOLLAR_X + V2_CARD_W - 10,
                          V2_CARD_Y + 74, &SatoshiMedium18);
}

static void drawHome(const V2RuntimeSnapshot& snapshot) {
    drawHeader(false, nullptr, snapshot);
    drawStockHero(snapshot);
    drawNewsHeadline(snapshot);
    drawBtcCard(snapshot);
    drawDollarCard(snapshot);
}

static void drawContext(const V2RuntimeSnapshot& snapshot, uint8_t selected) {
    drawHeader(true, "CONTEXTO", snapshot);
    const StockFocusedSnapshot* stock = selected < snapshot.stockCount ? &snapshot.stocks[selected] : nullptr;
    s_v2Sprite.setTextDatum(lgfx::top_left);
    s_v2Sprite.setTextColor(pal->highlight, pal->bg);
    s_v2Sprite.drawString(stock ? stock->symbol : "SIN DATO", V2_SAFE_INSET, 130, &PPNeueMachinaBold24);
    if (stock && stock->hasQuote) {
        s_v2Sprite.setTextColor(trendColor(stock->quote.changePct), pal->bg);
        char price[36];
        snprintf(price, sizeof(price), "$ %.0f", stock->quote.price);
        s_v2Sprite.drawString(price, V2_SAFE_INSET, 190, &PPNeueMachinaBold24);
        char move[36];
        snprintf(move, sizeof(move), "%+.2f%% HOY", stock->quote.changePct);
        s_v2Sprite.drawString(move, V2_SAFE_INSET, 250, &SatoshiMedium18);
        s_v2Sprite.setTextColor(pal->accent, pal->bg);
        char range[64];
        snprintf(range, sizeof(range), "RANGO %.2f / %.2f", stock->quote.dayLow, stock->quote.dayHigh);
        s_v2Sprite.drawString(range, V2_SAFE_INSET, 290, &Satoshi12);
        s_v2Sprite.drawFastHLine(V2_SAFE_INSET, 330, 416, pal->muted);
        if (snapshot.newsCount > 0) {
            s_v2Sprite.setTextColor(pal->muted, pal->bg);
            s_v2Sprite.drawString("NOTICIA DESTACADA", V2_SAFE_INSET, 340, &Satoshi9);
            drawHeadlineBlock(snapshot.news[0], V2_SAFE_INSET, 358, 416);
        } else {
            s_v2Sprite.setTextColor(pal->muted, pal->bg);
            s_v2Sprite.drawString("SIN NOTICIAS", V2_SAFE_INSET, 340, &Satoshi12);
        }
    } else {
        s_v2Sprite.drawString("CARGANDO DATOS REALES", V2_SAFE_INSET, 220, &Satoshi12);
    }
}

static void drawNewsReader(const V2RuntimeSnapshot& snapshot,
                           const V2RuntimeModel& model) {
    drawHeader(true, "NOTICIAS", snapshot);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    if (snapshot.newsCount == 0) {
        s_v2Sprite.setTextColor(pal->muted, pal->bg);
        s_v2Sprite.drawString(
            snapshot.newsFetching ? "CARGANDO NOTICIAS..." : "SIN NOTICIAS",
            V2_SAFE_INSET, 180, &SatoshiMedium18);
        return;
    }

    const uint8_t selected = model.selectedNews % snapshot.newsCount;
    const StockNews& news = snapshot.news[selected];
    const uint8_t focus = snapshot.stockCount
        ? snapshot.focusedStock % snapshot.stockCount : 0;
    const char* symbol = snapshot.stockCount
        ? snapshot.stocks[focus].symbol : "MERCADO";

    char position[32];
    snprintf(position, sizeof(position), "%s  %u/%u", symbol,
             static_cast<unsigned>(selected + 1),
             static_cast<unsigned>(snapshot.newsCount));
    s_v2Sprite.setTextColor(pal->accent, pal->bg);
    s_v2Sprite.drawString(position, V2_SAFE_INSET, 130, &Satoshi12);

    const WrappedHeadline wrapped = wrapHeadline(
        news.title, 416, NEWS_READER_MAX_LINES);
    s_v2Sprite.setTextColor(pal->text, pal->bg);
    for (uint8_t i = 0; i < wrapped.count; i++) {
        s_v2Sprite.drawString(wrapped.lines[i], V2_SAFE_INSET,
                              170 + i * 32, &SatoshiMedium18);
    }

    if (news.pubDate[0]) {
        s_v2Sprite.setTextColor(pal->muted, pal->bg);
        s_v2Sprite.drawString(news.pubDate, V2_SAFE_INSET, 390, &Satoshi9);
    }
    s_v2Sprite.drawFastHLine(V2_SAFE_INSET, 424, 416, pal->muted);
    s_v2Sprite.setTextDatum(lgfx::bottom_left);
    s_v2Sprite.drawString("TOCA PARA SIGUIENTE", V2_SAFE_INSET, 458, &Satoshi9);
}

static void drawWifiRecovery(const V2RuntimeSnapshot& snapshot) {
    drawLogo();
    s_v2Sprite.setTextDatum(lgfx::top_left);
    s_v2Sprite.setTextColor(pal->highlight, pal->bg);
    s_v2Sprite.drawString("SIN CONEXION", V2_SAFE_INSET, 132, &PPNeueMachinaBold24);
    s_v2Sprite.setTextColor(pal->accent, pal->bg);
    s_v2Sprite.drawString("No pudimos conectar a", V2_SAFE_INSET, 208, &Satoshi12);
    s_v2Sprite.setTextColor(pal->highlight, pal->bg);
    s_v2Sprite.drawString(snapshot.ssid[0] ? snapshot.ssid : "la red guardada",
                          V2_SAFE_INSET, 240, &SatoshiMedium18);
    s_v2Sprite.setTextColor(pal->muted, pal->bg);
    s_v2Sprite.drawString("Seguimos reintentando en segundo plano.",
                          V2_SAFE_INSET, 280, &Satoshi9);
    s_v2Sprite.fillSmoothRoundRect(V2_SAFE_INSET, 330, 416, 66, 8, pal->labelBg);
    s_v2Sprite.setTextColor(pal->labelFg, pal->labelBg);
    s_v2Sprite.setTextDatum(lgfx::middle_left);
    s_v2Sprite.drawString("CONFIGURAR OTRA RED", 52, 363, &SatoshiMedium18);
    drawChevron(426, 363, true, pal->labelFg);
    s_v2Sprite.setTextColor(pal->muted, pal->bg);
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

static void drawLoadingLogoProgress(uint8_t progress) {
    const uint16_t* logo = s_isLight
        ? lemon_v2_logo_black_120
        : lemon_v2_logo_light_120;
    int scale = 2;
    int logoW = 120 * scale;
    int logoH = 28 * scale;
    int logoX = (SCREEN_W - logoW) / 2;
    int logoY = V2_LOADING_LOGO_Y + (V2_LOADING_LOGO_H - logoH) / 2;
    for (int y = 0; y < 28; y++) {
        for (int x = 0; x < 120; x++) {
            uint16_t color = pgm_read_word(&logo[y * 120 + x]);
            if (color == 0x0000) continue;
            s_v2Sprite.fillRect(logoX + x * scale, logoY + y * scale, scale, scale, color);
        }
    }
    if (progress < 100) {
        int barW = (progress * 240) / 100;
        int barX = (SCREEN_W - 240) / 2;
        int barY = V2_LOADING_LOGO_Y + V2_LOADING_LOGO_H + 16;
        s_v2Sprite.drawRoundRect(barX, barY, 240, 6, 3, pal->muted);
        s_v2Sprite.fillRoundRect(barX, barY, barW, 6, 3, pal->accent);
    }
}

void v2UiDrawLoading(const char* status, uint8_t progress) {
    v2UiSetup();
    if (progress > 100) progress = 100;
    if (!s_loadingDrawn) {
        s_v2Sprite.fillSprite(pal->bg);
        s_loadingDrawn = true;
    }
    int logoY = V2_LOADING_LOGO_Y - 10;
    int logoH = 80;
    s_v2Sprite.fillRect(0, logoY, SCREEN_W, logoH, pal->bg);
    drawLoadingLogoProgress(progress);
    s_v2Sprite.fillRect(40, 278, 400, 42, pal->bg);
    s_v2Sprite.setTextColor(progress == 100 ? pal->highlight : pal->accent, pal->bg);
    s_v2Sprite.setTextDatum(lgfx::middle_center);
    s_v2Sprite.drawString(status, 240, 299, &Satoshi12);
    if (progress <= 5) {
        displayWaitVSync();
        uint32_t pushStartedUs = micros();
        s_v2Sprite.pushSprite(0, 0);
        displayRecordPush(SCREEN_W * SCREEN_H * 2,
                          micros() - pushStartedUs);
    } else {
        pushClip(80, logoY, 320, logoH + 50);
    }
}

void v2UiDraw(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model, bool clearFull) {
    v2UiSetup();
    clearDeferredClips();
    if (clearFull) s_v2Sprite.fillSprite(pal->bg);
    if (model.scene == V2_HOME) drawHome(snapshot);
    else if (model.scene == V2_MARKET_TAPE) drawContext(snapshot, model.selectedStock);
    else if (model.scene == V2_CONTEXT) drawContext(snapshot, model.selectedStock);
    else if (model.scene == V2_NEWS_READER) drawNewsReader(snapshot, model);
    else if (model.scene == V2_SETTINGS) v2DrawSettings(s_v2Sprite, snapshot, model);
    else drawWifiRecovery(snapshot);
    displayWaitVSync();
    uint32_t pushStartedUs = micros();
    s_v2Sprite.pushSprite(0, 0);
    displayRecordPush(SCREEN_W * SCREEN_H * 2,
                      micros() - pushStartedUs);
    strncpy(s_lastDrawnTime, snapshot.time, sizeof(s_lastDrawnTime) - 1);
    s_lastDrawnTime[sizeof(s_lastDrawnTime) - 1] = '\0';
    s_loadingDrawn = false;
}

void v2UiUpdateClock(const char* time, V2Scene scene) {
    if (!s_spriteReady || scene == V2_SETTINGS) return;
    if (strcmp(time, s_lastDrawnTime) == 0) return;
    constexpr int x = 170, y = 24, w = 140, h = 38;
    s_v2Sprite.fillRect(x, y, w, h, pal->bg);
    s_v2Sprite.setTextColor(pal->accent, pal->bg);
    s_v2Sprite.setTextDatum(lgfx::top_center);
    s_v2Sprite.drawString(time, SCREEN_W / 2, 32, &SatoshiMedium18);
    pushClip(x, y, w, h);
    strncpy(s_lastDrawnTime, time, sizeof(s_lastDrawnTime) - 1);
    s_lastDrawnTime[sizeof(s_lastDrawnTime) - 1] = '\0';
}

void v2UiUpdateHeader(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_HOME) return;
    constexpr int x = 328, y = 16, w = 120, h = 44;
    s_v2Sprite.fillRect(x, y, w, h, pal->bg);
    if (snapshot.otaAvailable) drawUpdateBadge();
    drawSettingsIcon(424, 36, pal->accent);
    pushClip(x, y, w, h);
}

void v2UiUpdateStatus(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
}

void v2UiUpdateStockHero(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_HOME) return;
    s_v2Sprite.fillRect(V2_HERO_TEXT_CLIP_X, V2_HERO_TEXT_CLIP_Y,
                        V2_HERO_TEXT_CLIP_W, V2_HERO_TEXT_CLIP_H, pal->bg);
    s_v2Sprite.fillRect(V2_HERO_GRAPH_CLIP_X, V2_HERO_GRAPH_CLIP_Y,
                        V2_HERO_GRAPH_CLIP_W, V2_HERO_GRAPH_CLIP_H, pal->bg);
    drawStockHero(snapshot);
    pushClip(V2_HERO_TEXT_CLIP_X, V2_HERO_TEXT_CLIP_Y,
             V2_HERO_TEXT_CLIP_W, V2_HERO_TEXT_CLIP_H);
    pushClip(V2_HERO_GRAPH_CLIP_X, V2_HERO_GRAPH_CLIP_Y,
             V2_HERO_GRAPH_CLIP_W, V2_HERO_GRAPH_CLIP_H);
}

void v2UiUpdateStockPrice(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_HOME) return;
    constexpr int px = V2_SAFE_INSET;
    constexpr int py = V2_HERO_CENTER_Y - 20;
    constexpr int pw = V2_HERO_PRICE_MAX_W;
    constexpr int ph = (V2_HERO_CHANGE_Y - py) + 26;
    s_v2Sprite.fillRect(px, py, pw, ph, pal->bg);
    uint8_t focus = snapshot.stockCount ? snapshot.focusedStock % snapshot.stockCount : 0;
    const StockFocusedSnapshot* stock = snapshot.stockCount ? &snapshot.stocks[focus] : nullptr;
    char price[40] = "--";
    if (stock && stock->hasQuote) snprintf(price, sizeof(price), "$%.0f", stock->quote.price);
    s_v2Sprite.setTextDatum(lgfx::middle_left);
    s_v2Sprite.setTextColor(stock && stock->hasQuote ? trendColor(stock->quote.changePct) : pal->muted,
                            pal->bg);
    s_v2Sprite.drawString(price, V2_SAFE_INSET, V2_HERO_CENTER_Y, &PPNeueMachinaBold24);
    s_v2Sprite.setTextDatum(lgfx::top_left);
    if (stock && stock->hasQuote) {
        char change[32];
        snprintf(change, sizeof(change), "%+.2f%% HOY", stock->quote.changePct);
        s_v2Sprite.setTextColor(trendColor(stock->quote.changePct), pal->bg);
        s_v2Sprite.drawString(change, V2_SAFE_INSET, V2_HERO_CHANGE_Y, &SatoshiMedium18);
    }
    pushClip(px, py, pw, ph);
}

void v2UiUpdateBtcCard(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_HOME) return;
    s_v2Sprite.fillRect(V2_BTC_X, V2_CARD_Y, V2_CARD_W, V2_CARD_H, pal->bg);
    drawBtcCard(snapshot);
    pushClip(V2_BTC_X, V2_CARD_Y, V2_CARD_W, V2_CARD_H);
}

void v2UiUpdateBtcPrice(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    v2UiUpdateBtcCard(snapshot, model);
}

void v2UiUpdateDollarCard(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_HOME) return;
    s_v2Sprite.fillRect(V2_DOLLAR_X, V2_CARD_Y, V2_CARD_W, V2_CARD_H, pal->bg);
    drawDollarCard(snapshot);
    pushClip(V2_DOLLAR_X, V2_CARD_Y, V2_CARD_W, V2_CARD_H);
}

void v2UiUpdateNews(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_HOME) return;
    drawNewsHeadline(snapshot);
    pushClip(V2_NEWS_CLIP_X, V2_NEWS_CLIP_Y, V2_NEWS_CLIP_W, V2_NEWS_CLIP_H);
}

void v2UiUpdatePriceOnly(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    v2UiUpdateBtcCard(snapshot, model);
}

void v2UiUpdatePriceDirect(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    v2UiUpdateBtcPrice(snapshot, model);
}

void v2UiUpdateHomeCards(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady || model.scene != V2_HOME) return;
    s_v2Sprite.fillRect(V2_CARDS_CLIP_X, V2_CARDS_CLIP_Y, V2_CARDS_CLIP_W, V2_CARDS_CLIP_H, pal->bg);
    drawBtcCard(snapshot);
    drawDollarCard(snapshot);
    pushClip(V2_CARDS_CLIP_X, V2_CARDS_CLIP_Y, V2_CARDS_CLIP_W, V2_CARDS_CLIP_H);
}

void v2UiUpdateStockCard(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    v2UiUpdateStockPrice(snapshot, model);
}

void v2UiUpdateTape(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    v2UiUpdateContext(snapshot, model);
}

void v2UiUpdateContext(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (!s_spriteReady) return;
    if (model.scene != V2_CONTEXT && model.scene != V2_MARKET_TAPE) return;
    constexpr int x = 28, y = 126, w = 424, h = 342;
    s_v2Sprite.fillRect(x, y, w, h, pal->bg);
    drawContext(snapshot, model.selectedStock);
    pushClip(x, y, w, h);
}

void v2UiUpdateData(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model) {
    if (model.scene == V2_HOME) {
        s_v2Sprite.fillRect(V2_HERO_CLIP_X, V2_HERO_CLIP_Y, V2_HERO_CLIP_W, V2_HERO_CLIP_H, pal->bg);
        drawStockHero(snapshot);
        s_v2Sprite.fillRect(V2_CARDS_CLIP_X, V2_CARDS_CLIP_Y, V2_CARDS_CLIP_W, V2_CARDS_CLIP_H, pal->bg);
        drawBtcCard(snapshot);
        drawDollarCard(snapshot);
        pushClip(V2_HERO_CLIP_X, V2_HERO_CLIP_Y, V2_HERO_CLIP_W, V2_HERO_CLIP_H);
        pushClip(V2_CARDS_CLIP_X, V2_CARDS_CLIP_Y, V2_CARDS_CLIP_W, V2_CARDS_CLIP_H);
    } else if (model.scene == V2_MARKET_TAPE || model.scene == V2_CONTEXT) {
        v2UiUpdateContext(snapshot, model);
    }
}

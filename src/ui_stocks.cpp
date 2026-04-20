#include "ui_stocks.h"
#include "ui_views.h"
#include "display_manager.h"
#include "colors.h"
#include "config.h"
#include "nvs_storage.h"
#include "stocks_client.h"
#include "ui_components.h"
#include "scheduler.h"
#include "data/satoshi_fonts.h"
#include "data_models.h"
#include "time_manager.h"
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <esp_task_wdt.h>

extern Scheduler scheduler;
extern uint8_t   taskStocks;

// ── Module state ──
static StockWatchlist  s_watchlist = {};
static StockQuote      s_quotes[STOCK_MAX_SYMBOLS] = {};
static uint8_t         s_quoteCount     = 0;
static SparklineData   s_sparks[STOCK_MAX_SYMBOLS] = {};   // one per watchlist slot
static uint8_t         s_focusedIdx     = 0;
static uint8_t         s_rrIdx          = 0;                // round-robin cursor
static uint8_t         s_priorityIdx    = 0xFF;             // user-tapped refresh (next fetch target)
static bool            s_dirty          = true;
static bool            s_everFetched    = false;
static bool            s_cacheDirty     = false;            // quotes changed since last NVS flush

static uint8_t findQuoteSlot(const char* sym) {
    for (uint8_t i = 0; i < s_quoteCount; i++) {
        if (strcmp(s_quotes[i].symbol, sym) == 0) return i;
    }
    return 0xFF;
}

static uint8_t watchlistIndexOf(const char* sym) {
    for (uint8_t i = 0; i < s_watchlist.count; i++) {
        if (strcmp(s_watchlist.symbols[i], sym) == 0) return i;
    }
    return 0xFF;
}

// One full-screen sprite in PSRAM, reused.
static LGFX_Sprite s_spr(&tft);
static bool        s_sprReady = false;

static bool ensureSprite() {
    if (s_sprReady) return true;
    s_spr.setPsram(true);
    s_spr.setColorDepth(16);
    if (!s_spr.createSprite(SCREEN_W, SCREEN_H)) {
        Serial.println("[Stocks] FATAL: cannot allocate 480x480 PSRAM sprite");
        return false;
    }
    s_sprReady = true;
    return true;
}

static void buildCsv(char* out, size_t cap) {
    out[0] = '\0';
    size_t used = 0;
    for (uint8_t i = 0; i < s_watchlist.count; i++) {
        if (used + STOCK_SYMBOL_LEN + 1 >= cap) break;
        if (used > 0) out[used++] = ',';
        size_t n = strlen(s_watchlist.symbols[i]);
        memcpy(out + used, s_watchlist.symbols[i], n);
        used += n;
    }
    out[used] = '\0';
}

static const StockQuote* findQuote(const char* sym) {
    for (uint8_t i = 0; i < s_quoteCount; i++) {
        if (s_quotes[i].valid && strcmp(s_quotes[i].symbol, sym) == 0) {
            return &s_quotes[i];
        }
    }
    return nullptr;
}

void stocksInit() {
    nvsLoadWatchlist(s_watchlist);
    Serial.printf("[Stocks] Watchlist loaded: %u symbols\n", (unsigned)s_watchlist.count);
    s_focusedIdx = 0;
    s_rrIdx = 0;
    s_priorityIdx = 0xFF;

    // Restore quote cache from NVS — gives us stale-but-visible data on boot
    // instead of a "Loading…" placeholder.
    nvsLoadStockQuotes(s_quotes, s_quoteCount);
    for (uint8_t i = 0; i < STOCK_MAX_SYMBOLS; i++) s_sparks[i].valid = false;
    s_everFetched = (s_quoteCount > 0);
    s_cacheDirty = false;

    s_dirty = true;
}

void stocksMarkDirty() { s_dirty = true; }

void stocksFetchTask() {
    if (s_watchlist.count == 0) return;

    // Pick target: priority (user-tapped ticker) wins over round-robin cursor.
    uint8_t target;
    if (s_priorityIdx != 0xFF && s_priorityIdx < s_watchlist.count) {
        target = s_priorityIdx;
        s_priorityIdx = 0xFF;
    } else {
        if (s_rrIdx >= s_watchlist.count) s_rrIdx = 0;
        target = s_rrIdx;
    }
    const char* sym = s_watchlist.symbols[target];

    StockQuote   tmpQuote = {};
    SparklineData tmpSpark = {};
    ApiResult r = fetchStockChart(sym, "1d", "5m", tmpQuote, tmpSpark);
    esp_task_wdt_reset();
    if (r == API_OK && tmpQuote.valid) {
        uint8_t slot = findQuoteSlot(tmpQuote.symbol);
        if (slot == 0xFF && s_quoteCount < STOCK_MAX_SYMBOLS) slot = s_quoteCount++;
        if (slot != 0xFF) s_quotes[slot] = tmpQuote;

        // Sparkline keyed by watchlist index (robust if quotes re-order).
        uint8_t wIdx = watchlistIndexOf(tmpQuote.symbol);
        if (wIdx != 0xFF) s_sparks[wIdx] = tmpSpark;

        s_everFetched = true;
        s_cacheDirty  = true;
        Serial.printf("[Stocks] updated %s (rr=%u, prio=%u)\n", sym, (unsigned)s_rrIdx, (unsigned)target);
    } else {
        Serial.printf("[Stocks] fetch failed for %s: %d\n", sym, (int)r);
    }

    // Advance round-robin only when this tick was a RR tick (not a priority refresh).
    if (target == s_rrIdx) {
        s_rrIdx = (s_rrIdx + 1) % s_watchlist.count;
        // Persist quotes at the end of each full loop so NVS writes stay bounded.
        if (s_rrIdx == 0 && s_cacheDirty) {
            nvsSaveStockQuotes(s_quotes, s_quoteCount);
            s_cacheDirty = false;
            Serial.printf("[Stocks] quote cache flushed to NVS (%u symbols)\n", (unsigned)s_quoteCount);
        }
    }
    s_dirty = true;
}

// ── Rendering ──

static void drawEmptyState(LGFX_Sprite& spr) {
    spr.fillScreen(Colors::BG_BASE);
    spr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString("STOCKS", SCREEN_W / 2, 220, &SatoshiBold24);
    spr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    const char* msg = s_everFetched ? "No data" : "Loading...";
    spr.drawString(msg, SCREEN_W / 2, 260, &Satoshi12);
    spr.setTextDatum(lgfx::top_left);
}

static void drawHeader(LGFX_Sprite& spr) {
    // Slim header: time (left), "STOCKS" label (center), page (right)
    spr.fillRect(0, 0, SCREEN_W, 44, Colors::BG_OVERLAY);
    const char* t = timeReady() ? getTimeStr(true) : "--:--";
    spr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_OVERLAY);
    spr.setTextDatum(lgfx::middle_left);
    spr.drawString(t, 16, 22, &Satoshi12);

    spr.setTextColor(Colors::LEMON_GREEN, Colors::BG_OVERLAY);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString("STOCKS", SCREEN_W / 2, 22, &Satoshi12);

    char pageLabel[16];
    snprintf(pageLabel, sizeof(pageLabel), "%u/%u",
             (unsigned)(s_focusedIdx + 1), (unsigned)s_watchlist.count);
    spr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_OVERLAY);
    spr.setTextDatum(lgfx::middle_right);
    spr.drawString(pageLabel, SCREEN_W - 16, 22, &Satoshi12);

    spr.setTextDatum(lgfx::top_left);
}

static const lgfx::IFont* fontForPrice(float price) {
    // Step down the price font when the number gets wide, so it doesn't clip
    // the card edges. $10,000+ uses Bold24, others use Bold40.
    return (price >= 10000.0f) ? &SatoshiBold24 : &SatoshiBold40;
}

static void drawStockCard(LGFX_Sprite& spr, const StockQuote& q) {
    // Card background
    const int cardX = 16, cardY = 60, cardW = 448, cardH = 380;
    drawCard(spr, cardX, cardY, cardW, cardH, 16, Colors::BG_CARD, Colors::CARD_BORDER);

    // Symbol (top-left)
    spr.setTextColor(Colors::LEMON_GREEN, Colors::BG_CARD);
    spr.setTextDatum(lgfx::top_left);
    spr.drawString(q.symbol, cardX + 20, cardY + 16, &SatoshiBold24);

    // Name (right of symbol, smaller)
    if (q.name[0]) {
        spr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
        spr.drawString(q.name, cardX + 20, cardY + 52, &Satoshi12);
    }

    // Price (centered horizontally, mid-card)
    char priceStr[32];
    if (q.price < 1.0f) snprintf(priceStr, sizeof(priceStr), "$%.4f", q.price);
    else if (q.price < 100.0f) snprintf(priceStr, sizeof(priceStr), "$%.2f", q.price);
    else snprintf(priceStr, sizeof(priceStr), "$%.2f", q.price);

    spr.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_CARD);
    spr.setTextDatum(lgfx::middle_center);
    spr.drawString(priceStr, SCREEN_W / 2, cardY + 130, fontForPrice(q.price));

    // Change (below price)
    bool up = q.change >= 0.0f;
    char changeStr[48];
    snprintf(changeStr, sizeof(changeStr), "%s%.2f  %s%.2f%%",
             up ? "+" : "", q.change, up ? "+" : "", q.changePct);
    spr.setTextColor(up ? Colors::POSITIVE : Colors::NEGATIVE, Colors::BG_CARD);
    spr.drawString(changeStr, SCREEN_W / 2, cardY + 180, &SatoshiMedium18);

    // Sparkline
    const int chartX = cardX + 20;
    const int chartY = cardY + 210;
    const int chartW = cardW - 40;
    const int chartH = 110;
    uint8_t wIdx = watchlistIndexOf(q.symbol);
    const SparklineData* spk = (wIdx != 0xFF) ? &s_sparks[wIdx] : nullptr;
    if (spk && spk->valid && spk->count >= 2) {
        drawSparkline(spr, chartX, chartY, chartW, chartH,
                      *spk,
                      up ? Colors::POSITIVE : Colors::NEGATIVE,
                      up ? Colors::CHART_FILL : Colors::BADGE_BG_NEG);
    } else {
        spr.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_CARD);
        spr.setTextDatum(lgfx::middle_center);
        spr.drawString("--- chart loading ---", chartX + chartW / 2, chartY + chartH / 2, &Satoshi9);
    }

    // Day high / low row
    const int rangeY = cardY + cardH - 30;
    spr.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_CARD);
    spr.setTextDatum(lgfx::middle_left);
    char hl[48];
    if (isfinite(q.dayHigh) && isfinite(q.dayLow)) {
        snprintf(hl, sizeof(hl), "H $%.2f", q.dayHigh);
        spr.drawString(hl, cardX + 20, rangeY, &Satoshi12);
        snprintf(hl, sizeof(hl), "L $%.2f", q.dayLow);
        spr.setTextDatum(lgfx::middle_right);
        spr.drawString(hl, cardX + cardW - 20, rangeY, &Satoshi12);
    }

    spr.setTextDatum(lgfx::top_left);
}

void stocksDrawAll() {
    // First visit: kick the scheduler to fetch immediately instead of waiting
    // for the 60 s cadence. Safe to call repeatedly — requestRun is idempotent.
    if (!s_everFetched) scheduler.requestRun(taskStocks);

    if (!ensureSprite()) {
        // Fallback: direct-to-tft minimal state so the view isn't blank.
        tft.fillScreen(Colors::BG_BASE);
        tft.setTextColor(Colors::NEGATIVE, Colors::BG_BASE);
        tft.setTextDatum(lgfx::middle_center);
        tft.drawString("Stocks: no PSRAM", SCREEN_W / 2, SCREEN_H / 2);
        tft.setTextDatum(lgfx::top_left);
        return;
    }

    drawHeader(s_spr);

    if (s_watchlist.count == 0 || s_focusedIdx >= s_watchlist.count) {
        drawEmptyState(s_spr);
    } else {
        const char* sym = s_watchlist.symbols[s_focusedIdx];
        const StockQuote* q = findQuote(sym);
        if (q && q->valid) {
            drawStockCard(s_spr, *q);
        } else {
            drawEmptyState(s_spr);
        }
    }

    s_spr.pushSprite(0, 0);
    viewsDrawDotsOverlay();
    s_dirty = false;
}

void stocksHandleTouch(const TouchEvent& evt) {
    if (evt.gesture != TOUCH_SWIPE_LEFT && evt.gesture != TOUCH_SWIPE_RIGHT) return;
    if (s_watchlist.count <= 1) return;
    int8_t dir = (evt.gesture == TOUCH_SWIPE_LEFT) ? +1 : -1;
    int next = (int)s_focusedIdx + dir;
    if (next < 0) next = s_watchlist.count - 1;
    if (next >= s_watchlist.count) next = 0;
    s_focusedIdx = (uint8_t)next;
    // Request a priority refresh of the newly focused ticker — round-robin
    // keeps rotating after.
    s_priorityIdx = s_focusedIdx;
    scheduler.requestRun(taskStocks);
    s_dirty = true;
    stocksDrawAll();                            // paint from cache immediately
}

uint8_t stocksGetFocusedIdx() { return s_focusedIdx; }
uint8_t stocksGetWatchlistCount() { return s_watchlist.count; }

const char* stocksGetFocusedSymbol() {
    if (s_watchlist.count == 0 || s_focusedIdx >= s_watchlist.count) return nullptr;
    return s_watchlist.symbols[s_focusedIdx];
}

const StockQuote* stocksGetFocusedQuote() {
    const char* sym = stocksGetFocusedSymbol();
    if (!sym) return nullptr;
    uint8_t slot = findQuoteSlot(sym);
    if (slot == 0xFF) return nullptr;
    return &s_quotes[slot];
}

const SparklineData* stocksGetFocusedSpark() {
    if (s_watchlist.count == 0 || s_focusedIdx >= s_watchlist.count) return nullptr;
    const SparklineData* sp = &s_sparks[s_focusedIdx];
    return sp->valid ? sp : nullptr;
}

void stocksAdvanceFocused() {
    if (s_watchlist.count <= 1) return;
    s_focusedIdx = (s_focusedIdx + 1) % s_watchlist.count;
    s_priorityIdx = s_focusedIdx;
    scheduler.requestRun(taskStocks);
    s_dirty = true;
}

void stocksTick() {
    if (s_dirty) stocksDrawAll();
}

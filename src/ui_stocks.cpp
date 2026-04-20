#include "ui_stocks.h"
#include "config.h"
#include "nvs_storage.h"
#include "stocks_client.h"
#include "scheduler.h"
#include "data_models.h"
#include <Arduino.h>
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

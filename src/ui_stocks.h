#pragma once

#include <cstddef>
#include <cstdint>
#include "data_models.h"

// One-time init: loads the watchlist from NVS.
void stocksInit();

// Scheduler task body: refreshes quotes + focused-symbol chart.
// Safe to call at any time (non-blocking, single-shot).
void stocksFetchTask();

// Force a redraw next time the Stocks view is active (e.g. after watchlist
// edit via captive portal).
void stocksMarkDirty();

// Enable network refreshes while the Stocks card is visible. Disabling it
// cancels queued burst work but lets any in-flight HTTP fetch finish cleanly.
void stocksSetActive(bool active);

// ── Accessors used by ui_dashboard to render the compact Z2 card ──
uint8_t             stocksGetFocusedIdx();
uint8_t             stocksGetWatchlistCount();
const char*         stocksGetFocusedSymbol();     // nullptr if watchlist empty
const StockQuote*   stocksGetFocusedQuote();      // nullptr if no cache for focused symbol
const SparklineData* stocksGetFocusedSpark();     // nullptr if no sparkline cached

struct StockFocusedSnapshot {
    uint8_t focusedIdx = 0;
    uint8_t watchlistCount = 0;
    char symbol[STOCK_SYMBOL_LEN] = {};
    char status[32] = {};
    StockQuote quote = {};
    SparklineData spark = {};
    bool hasQuote = false;
    bool hasSpark = false;
    bool fetching = false;
};

bool stocksGetFocusedSnapshot(StockFocusedSnapshot& out);
bool stocksGetSnapshotAt(uint8_t watchlistIndex, StockFocusedSnapshot& out);

// Advance focused ticker (wraps). Triggers a priority refresh.
void stocksAdvanceFocused();

// Kick a burst refresh that fetches every watchlist symbol back-to-back
// on the worker. Used when the user enters Stocks mode so all charts
// populate within seconds instead of waiting N × 60s for the scheduler
// to round-robin through them.
void stocksRequestBurst();

// True while the async worker has a fetch in flight. Rendering can use
// this to show a "loading" indicator.
bool stocksIsFetching();

// Tear down the async worker + its TLS session. Used before OTA so the
// flash TLS handshake can claim the DRAM that the stocks session holds.
void stocksStop();

// Last-fetch diagnostic line (empty when no fetch happened yet). Exposed
// for the stocks card to print on the "chart loading" placeholder so we
// can diagnose parse failures without a serial monitor.
const char* stocksLastDebug();

const char* stocksGetFocusedStatusText();

// Returns true exactly once after an async fetch updates the cache. Main
// loop polls this to trigger a Z2 redraw (the worker runs on core 0 and
// can't touch the TFT).
bool stocksConsumeDirty();

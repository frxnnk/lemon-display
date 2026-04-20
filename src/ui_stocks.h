#pragma once

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

// ── Accessors used by ui_dashboard to render the compact Z2 card ──
uint8_t             stocksGetFocusedIdx();
uint8_t             stocksGetWatchlistCount();
const char*         stocksGetFocusedSymbol();     // nullptr if watchlist empty
const StockQuote*   stocksGetFocusedQuote();      // nullptr if no cache for focused symbol
const SparklineData* stocksGetFocusedSpark();     // nullptr if no sparkline cached

// Advance focused ticker (wraps). Triggers a priority refresh.
void stocksAdvanceFocused();

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

#include "ui_stocks.h"
#include "config.h"
#include "nvs_storage.h"
#include "stocks_client.h"
#include "scheduler.h"
#include "data_models.h"
#include <Arduino.h>
#include <cstring>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

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

// ── Async worker (core 0) ──
// The scheduler runs on core 1 (main loop); fetchStockChart blocks on
// Yahoo's TLS handshake for 1-5s and would freeze the UI. Running it on
// a dedicated FreeRTOS task on core 0 keeps the main loop free.
static TaskHandle_t   s_workerHandle  = nullptr;
static volatile bool  s_fetching      = false;
static char           s_lastDbg[64]   = "";
// Burst mode — bitmask of watchlist slots still to fetch in the current
// burst. The worker drains this one slot at a time (focused first, then
// lowest-numbered bit). Failed fetches stay set until retries exhaust,
// so MSTR (which flakes on fragmented heap) gets re-tried AFTER other
// symbols free their TLS buffers instead of dying silently.
#define STOCKS_MAX_BURST_RETRIES 2
static volatile uint8_t s_burstPending = 0;
static uint8_t          s_burstRetryCount[STOCK_MAX_SYMBOLS] = {0};

static void stocksWorkerTask(void*);

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

    // Restore quote + sparkline caches from NVS — gives us stale-but-visible
    // data on boot instead of a "Loading…" placeholder. Sparks are keyed by
    // symbol so watchlist reorders don't misalign charts.
    nvsLoadStockQuotes(s_quotes, s_quoteCount);
    nvsLoadStockSparks(s_sparks, s_watchlist);
    s_everFetched = (s_quoteCount > 0);
    s_cacheDirty = false;

    s_dirty = true;

    // Spawn the worker once — pinned to core 0 so Yahoo's TLS handshake
    // never runs on the main loop. 8KB stack fits HTTPClient + mbedtls +
    // ArduinoJson comfortably.
    if (!s_workerHandle) {
        xTaskCreatePinnedToCore(stocksWorkerTask, "stocksW", 8192,
                                nullptr, 1, &s_workerHandle, 0);
    }
}

void stocksMarkDirty() { s_dirty = true; }

// Actual fetch body — runs on the worker task (core 0).
static void stocksFetchBody() {
    if (s_watchlist.count == 0) return;

    // Pick target: priority (user tap) wins, then the burst pending mask
    // (preferring focused if still queued, otherwise lowest-numbered slot),
    // finally the regular RR cursor. Tracked source flags so we only
    // advance the RR cursor when we're actually on the RR path.
    uint8_t target = 0xFF;
    bool fromBurst = false;
    bool burstWasActive = (s_burstPending != 0);

    if (s_priorityIdx != 0xFF && s_priorityIdx < s_watchlist.count) {
        target = s_priorityIdx;
        s_priorityIdx = 0xFF;
        if (burstWasActive && (s_burstPending & (uint8_t)(1u << target))) {
            fromBurst = true;
        }
    } else if (burstWasActive) {
        if ((s_burstPending & (uint8_t)(1u << s_focusedIdx)) &&
            s_focusedIdx < s_watchlist.count) {
            target = s_focusedIdx;
        } else {
            for (uint8_t i = 0; i < STOCK_MAX_SYMBOLS; i++) {
                if (s_burstPending & (uint8_t)(1u << i)) { target = i; break; }
            }
        }
        fromBurst = true;
    } else {
        if (s_rrIdx >= s_watchlist.count) s_rrIdx = 0;
        target = s_rrIdx;
    }

    if (target >= s_watchlist.count) return;
    const char* sym = s_watchlist.symbols[target];

    StockQuote   tmpQuote = {};
    SparklineData tmpSpark = {};
    ApiResult r = fetchStockChart(sym, "1d", "5m", tmpQuote, tmpSpark);
    esp_task_wdt_reset();
    snprintf(s_lastDbg, sizeof(s_lastDbg),
             "r=%d c=%d b=%d v=%d sv=%d sc=%d",
             (int)r,
             stocksClientLastCode(),
             stocksClientLastBytes(),
             (int)tmpQuote.valid,
             (int)tmpSpark.valid, (int)tmpSpark.count);
    if (r == API_OK && tmpQuote.valid) {
        uint8_t slot = findQuoteSlot(tmpQuote.symbol);
        if (slot == 0xFF && s_quoteCount < STOCK_MAX_SYMBOLS) slot = s_quoteCount++;
        if (slot != 0xFF) s_quotes[slot] = tmpQuote;

        uint8_t wIdx = watchlistIndexOf(tmpQuote.symbol);
        if (wIdx != 0xFF) s_sparks[wIdx] = tmpSpark;

        s_burstRetryCount[target] = 0;
        s_burstPending &= (uint8_t)~(1u << target);
        s_everFetched = true;
        s_cacheDirty  = true;
        Serial.printf("[Stocks] updated %s (target=%u pending=0x%02X)\n",
                      sym, (unsigned)target, (unsigned)s_burstPending);
    } else {
        Serial.printf("[Stocks] fetch failed for %s: %d (retry %u/%u)\n",
                      sym, (int)r,
                      (unsigned)s_burstRetryCount[target],
                      (unsigned)STOCKS_MAX_BURST_RETRIES);
        // Keep the slot pending for another attempt — worker will cycle
        // through other pending slots first so mbedtls can free buffers
        // between MSTR retries. Budget capped to avoid runaway loops.
        if (s_burstRetryCount[target] < STOCKS_MAX_BURST_RETRIES) {
            s_burstRetryCount[target]++;
            s_burstPending |= (uint8_t)(1u << target);
        } else {
            s_burstPending &= (uint8_t)~(1u << target);
        }
    }

    // Regular RR cursor advance — only when this fetch came from the RR
    // path (burst pending mask drives its own ordering). NVS flush happens
    // on RR wrap OR on burst drain, below.
    bool rrCycleComplete = false;
    if (!fromBurst && target == s_rrIdx) {
        s_rrIdx = (s_rrIdx + 1) % s_watchlist.count;
        if (s_rrIdx == 0) rrCycleComplete = true;
    }

    bool burstJustDrained = (burstWasActive && s_burstPending == 0);
    if (s_cacheDirty && (rrCycleComplete || burstJustDrained)) {
        nvsSaveStockQuotes(s_quotes, s_quoteCount);
        nvsSaveStockSparks(s_sparks, s_watchlist);
        s_cacheDirty = false;
        Serial.printf("[Stocks] quote+spark cache flushed to NVS (%u symbols)\n",
                      (unsigned)s_quoteCount);
    }

    s_dirty = true;
}

static void stocksWorkerTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_fetching = true;
        stocksFetchBody();
        s_fetching = false;

        // Chain as long as the burst pending mask has slots to serve (a
        // mix of unfetched and retry-queued slots). Brief pause lets the
        // UI repaint the just-finished symbol and gives the TCP/TLS
        // session time to tear down before the next handshake.
        if (s_burstPending != 0) {
            vTaskDelay(pdMS_TO_TICKS(400));
            xTaskNotifyGive(s_workerHandle);
        }
    }
}

// Called by the scheduler on core 1. Just notifies the worker — no blocking.
void stocksFetchTask() {
    if (!s_workerHandle) return;
    xTaskNotifyGive(s_workerHandle);
}

// Request a burst refresh — fetches every watchlist symbol back-to-back
// (one RR step per fetch). Called when the user first enters Stocks mode
// so all charts populate within ~N×1-3s instead of N×60s. Kicks off with
// the currently focused symbol so the user sees *their* chart fill in on
// the first fetch (~2-3s) instead of waiting for the RR cursor to reach
// it after 2-5 earlier fetches.
void stocksRequestBurst() {
    if (!s_workerHandle) return;
    if (s_watchlist.count == 0) return;
    // Queue every watchlist slot; the fetch body will prefer focused first,
    // then lowest-numbered pending slot. Retries are handled transparently
    // by the same mask (failed slots stay set until budget exhausts).
    uint8_t mask = 0;
    for (uint8_t i = 0; i < s_watchlist.count && i < STOCK_MAX_SYMBOLS; i++) {
        mask |= (uint8_t)(1u << i);
    }
    s_burstPending = mask;
    s_priorityIdx = 0xFF;
    memset(s_burstRetryCount, 0, sizeof(s_burstRetryCount));
    Serial.printf("[Stocks] burst refresh requested (pending=0x%02X focus=%u)\n",
                  (unsigned)mask, (unsigned)s_focusedIdx);
    xTaskNotifyGive(s_workerHandle);
}

bool stocksIsFetching() { return s_fetching; }

const char* stocksLastDebug() { return s_lastDbg; }

bool stocksConsumeDirty() {
    if (!s_dirty) return false;
    s_dirty = false;
    return true;
}

void stocksStop() {
    if (s_workerHandle) {
        TaskHandle_t h = s_workerHandle;
        s_workerHandle = nullptr;   // stocksFetchTask sees null and no-ops
        vTaskDelete(h);
        Serial.println("[Stocks] worker stopped");
    }
    s_fetching = false;
    s_burstPending = 0;
    // Release the worker's dedicated TLS session too — frees ~30KB DRAM.
    stocksClientStop();
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

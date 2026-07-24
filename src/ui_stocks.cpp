#include "ui_stocks.h"
#include "config.h"
#include "nvs_storage.h"
#include "api_client.h"
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
static uint32_t        s_lastOkMs       = 0;                // millis() of last successful fetch

// ── Async worker (core 0) ──
// The scheduler runs on core 1 (main loop); fetchStockChart blocks on
// Yahoo's TLS handshake for 1-5s and would freeze the UI. Running it on
// a dedicated FreeRTOS task on core 0 keeps the main loop free.
static TaskHandle_t   s_workerHandle  = nullptr;
static volatile bool  s_active        = false;
static volatile bool  s_fetching      = false;
static char           s_lastDbg[64]   = "";
enum StockLoadState : uint8_t {
    STOCK_LOAD_IDLE = 0,
    STOCK_LOAD_FETCHING,
    STOCK_LOAD_READY,
    STOCK_LOAD_RATE_LIMITED,
    STOCK_LOAD_ERROR,
};
static uint8_t        s_loadState[STOCK_MAX_SYMBOLS] = {};
static int            s_lastCodeBySlot[STOCK_MAX_SYMBOLS] = {};
static uint32_t       s_retryAtMs[STOCK_MAX_SYMBOLS] = {};
static uint32_t       s_chainDelayMs = 1200;
static char           s_statusBuf[32] = "";
static SemaphoreHandle_t s_lock = nullptr;
// Burst mode — bitmask of watchlist slots still to fetch in the current
// burst. The worker drains this one slot at a time (focused first, then
// lowest-numbered bit). Failed fetches stay set until retries exhaust,
// so MSTR (which flakes on fragmented heap) gets re-tried AFTER other
// symbols free their TLS buffers instead of dying silently.
#define STOCKS_MAX_BURST_RETRIES 2
static volatile uint8_t s_burstPending = 0;
static uint8_t          s_burstRetryCount[STOCK_MAX_SYMBOLS] = {0};
static const uint32_t   STOCKS_REGULAR_REFRESH_MIN_MS = 10UL * 60UL * 1000UL;

static void stocksWorkerTask(void*);

static void ensureStocksWorkerRunning() {
    if (s_workerHandle) return;
    xTaskCreatePinnedToCore(stocksWorkerTask, "stocksW", 8192,
                            nullptr, 1, &s_workerHandle, 0);
}

static void ensureStocksLock() {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
}

static bool lockStocks(TickType_t wait = pdMS_TO_TICKS(50)) {
    ensureStocksLock();
    return s_lock && xSemaphoreTake(s_lock, wait) == pdTRUE;
}

static void unlockStocks() {
    if (s_lock) xSemaphoreGive(s_lock);
}

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

static bool slotRetryReady(uint8_t idx, uint32_t nowMs) {
    return idx < STOCK_MAX_SYMBOLS && (s_retryAtMs[idx] == 0 || s_retryAtMs[idx] <= nowMs);
}

static uint32_t pendingRetryDelayMs(uint32_t nowMs) {
    uint32_t minDelay = 0;
    for (uint8_t i = 0; i < s_watchlist.count && i < STOCK_MAX_SYMBOLS; i++) {
        if (!(s_burstPending & (uint8_t)(1u << i))) continue;
        if (slotRetryReady(i, nowMs)) return 0;
        uint32_t delayMs = s_retryAtMs[i] - nowMs;
        if (minDelay == 0 || delayMs < minDelay) minDelay = delayMs;
    }
    return minDelay;
}

static const char* formatStatusTextLocked(uint8_t idx, char* out, size_t outSize) {
    if (!out || outSize == 0) return nullptr;
    out[0] = '\0';
    if (s_watchlist.count == 0 || idx >= s_watchlist.count) return nullptr;

    switch (s_loadState[idx]) {
        case STOCK_LOAD_FETCHING:
            strncpy(out, "Cargando...", outSize - 1);
            break;
        case STOCK_LOAD_RATE_LIMITED: {
            uint32_t now = millis();
            if (s_retryAtMs[idx] > now) {
                uint32_t sec = (s_retryAtMs[idx] - now + 999) / 1000;
                snprintf(out, outSize, "Reintento %lus", (unsigned long)sec);
            } else if (s_burstPending & (uint8_t)(1u << idx)) {
                strncpy(out, "Reintentando...", outSize - 1);
            } else {
                strncpy(out, "429 Yahoo", outSize - 1);
            }
            break;
        }
        case STOCK_LOAD_ERROR:
            if (s_lastCodeBySlot[idx] == 200) {
                strncpy(out, "Sin grafico", outSize - 1);
            } else if (s_lastCodeBySlot[idx] > 0) {
                snprintf(out, outSize, "HTTP %d", s_lastCodeBySlot[idx]);
            } else {
                strncpy(out, "Error de carga", outSize - 1);
            }
            break;
        case STOCK_LOAD_IDLE:
            strncpy(out, "Esperando...", outSize - 1);
            break;
        default:
            return nullptr;
    }

    out[outSize - 1] = '\0';
    return out;
}

void stocksInit() {
    lockStocks(portMAX_DELAY);
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
    memset(s_loadState, 0, sizeof(s_loadState));
    memset(s_lastCodeBySlot, 0, sizeof(s_lastCodeBySlot));
    memset(s_retryAtMs, 0, sizeof(s_retryAtMs));
    s_chainDelayMs = 1200;
    for (uint8_t i = 0; i < s_watchlist.count && i < STOCK_MAX_SYMBOLS; i++) {
        s_loadState[i] = s_sparks[i].valid ? STOCK_LOAD_READY : STOCK_LOAD_IDLE;
    }

    s_dirty = true;

    // Spawn the worker once — pinned to core 0 so Yahoo's TLS handshake
    // never runs on the main loop. 8KB stack fits HTTPClient + mbedtls +
    // ArduinoJson comfortably.
    unlockStocks();

    ensureStocksWorkerRunning();
}

void stocksMarkDirty() {
    if (lockStocks()) {
        s_dirty = true;
        unlockStocks();
    }
}

void stocksSetActive(bool active) {
    if (!lockStocks(portMAX_DELAY)) return;
    s_active = active;
    if (!active) {
        s_burstPending = 0;
        s_priorityIdx = 0xFF;
        memset(s_burstRetryCount, 0, sizeof(s_burstRetryCount));
        memset(s_retryAtMs, 0, sizeof(s_retryAtMs));
        s_chainDelayMs = 1200;
        s_dirty = true;
    }
    unlockStocks();
    if (active) ensureStocksWorkerRunning();
}

// Actual fetch body — runs on the worker task (core 0).
static void stocksFetchBody() {
    uint8_t target = 0xFF;
    bool fromBurst = false;
    bool burstWasActive = false;
    char sym[STOCK_SYMBOL_LEN] = "";
    uint32_t nowMs = millis();

    if (!lockStocks(pdMS_TO_TICKS(500))) return;
    if (!s_active || s_watchlist.count == 0) {
        unlockStocks();
        return;
    }

    burstWasActive = (s_burstPending != 0);

    if (s_priorityIdx != 0xFF && s_priorityIdx < s_watchlist.count &&
        slotRetryReady(s_priorityIdx, nowMs)) {
        target = s_priorityIdx;
        s_priorityIdx = 0xFF;
        if (burstWasActive && (s_burstPending & (uint8_t)(1u << target))) {
            fromBurst = true;
        }
    } else if (burstWasActive) {
        if ((s_burstPending & (uint8_t)(1u << s_focusedIdx)) &&
            slotRetryReady(s_focusedIdx, nowMs) &&
            s_focusedIdx < s_watchlist.count) {
            target = s_focusedIdx;
        } else {
            for (uint8_t i = 0; i < STOCK_MAX_SYMBOLS; i++) {
                if ((s_burstPending & (uint8_t)(1u << i)) && slotRetryReady(i, nowMs)) {
                    target = i;
                    break;
                }
            }
        }
        fromBurst = true;
    } else {
        if (s_rrIdx >= s_watchlist.count) s_rrIdx = 0;
        target = s_rrIdx;
        s_burstRetryCount[target] = 0;
    }

    if (target == 0xFF) {
        uint32_t delayMs = pendingRetryDelayMs(nowMs);
        if (delayMs == 0 && s_priorityIdx != 0xFF && s_priorityIdx < s_watchlist.count &&
            !slotRetryReady(s_priorityIdx, nowMs)) {
            delayMs = s_retryAtMs[s_priorityIdx] - nowMs;
        }
        if (delayMs > 0) s_chainDelayMs = delayMs;
        unlockStocks();
        return;
    }

    if (target >= s_watchlist.count) {
        unlockStocks();
        return;
    }
    strncpy(sym, s_watchlist.symbols[target], sizeof(sym) - 1);
    sym[sizeof(sym) - 1] = '\0';
    s_loadState[target] = STOCK_LOAD_FETCHING;
    s_dirty = true;
    unlockStocks();

    StockQuote   tmpQuote = {};
    SparklineData tmpSpark = {};
    ApiResult r = fetchStockChart(sym, "1d", "5m", tmpQuote, tmpSpark);
    esp_task_wdt_reset();
    char dbg[64];
    int lastCode = stocksClientLastCode();
    snprintf(dbg, sizeof(dbg),
             "%s r=%d c=%d b=%d h=%uk m=%uk",
             sym,
             (int)r,
             lastCode,
             stocksClientLastBytes(),
             (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getMaxAllocHeap() / 1024));

    if (!lockStocks(pdMS_TO_TICKS(500))) return;
    strncpy(s_lastDbg, dbg, sizeof(s_lastDbg) - 1);
    s_lastDbg[sizeof(s_lastDbg) - 1] = '\0';
    s_lastCodeBySlot[target] = lastCode;

    if (r == API_OK && tmpQuote.valid) {
        uint8_t slot = findQuoteSlot(tmpQuote.symbol);
        if (slot == 0xFF && s_quoteCount < STOCK_MAX_SYMBOLS) slot = s_quoteCount++;
        if (slot != 0xFF) s_quotes[slot] = tmpQuote;
    }

    bool chartOk = (tmpSpark.valid && tmpSpark.count >= 2);
    if (r == API_OK && tmpQuote.valid && chartOk) {
        uint8_t wIdx = watchlistIndexOf(tmpQuote.symbol);
        if (wIdx != 0xFF) s_sparks[wIdx] = tmpSpark;

        s_burstRetryCount[target] = 0;
        s_burstPending &= (uint8_t)~(1u << target);
        s_loadState[target] = STOCK_LOAD_READY;
        s_retryAtMs[target] = 0;
        s_chainDelayMs = 400;
        s_everFetched = true;
        s_cacheDirty  = true;
        s_lastOkMs    = millis();
        Serial.printf("[Stocks] updated %s (target=%u pending=0x%02X)\n",
                      sym, (unsigned)target, (unsigned)s_burstPending);
    } else {
        if (r == API_OK && tmpQuote.valid && !chartOk) {
            r = API_PARSE_ERROR;
        }
        Serial.printf("[Stocks] fetch failed for %s: %d (retry %u/%u)\n",
                      sym, (int)r,
                      (unsigned)s_burstRetryCount[target],
                      (unsigned)STOCKS_MAX_BURST_RETRIES);
        s_loadState[target] = (r == API_RATE_LIMITED) ? STOCK_LOAD_RATE_LIMITED : STOCK_LOAD_ERROR;
        if (fromBurst && s_burstRetryCount[target] < STOCKS_MAX_BURST_RETRIES) {
            s_burstRetryCount[target]++;
            s_burstPending |= (uint8_t)(1u << target);
            s_retryAtMs[target] = millis() + ((r == API_RATE_LIMITED) ? 8000UL : 2500UL);
            s_chainDelayMs = 400;
        } else if (fromBurst) {
            s_burstPending &= (uint8_t)~(1u << target);
            s_chainDelayMs = 400;
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
    unlockStocks();
}

static void stocksWorkerTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (lockStocks()) {
            s_fetching = true;
            unlockStocks();
        } else {
            s_fetching = true;
        }
        stocksFetchBody();
        uint8_t pending = 0;
        uint32_t delayMs = 1200;
        bool active = false;
        if (lockStocks()) {
            active = s_active;
            pending = active ? s_burstPending : 0;
            delayMs = s_chainDelayMs;
            if (pending == 0) s_fetching = false;
            unlockStocks();
        } else {
            s_fetching = false;
        }

        // Chain as long as the burst pending mask has slots to serve (a
        // mix of unfetched and retry-queued slots). Brief pause lets the
        // UI repaint the just-finished symbol and gives the TCP/TLS
        // session time to tear down before the next handshake.
        if (pending != 0 && active) {
            if (delayMs < 200) delayMs = 200;
            vTaskDelay(pdMS_TO_TICKS(delayMs));
            TaskHandle_t worker = nullptr;
            if (lockStocks()) {
                worker = s_workerHandle;
                unlockStocks();
            }
            if (worker) xTaskNotifyGive(worker);
        }
    }
}

// Called by the scheduler on core 1. Just notifies the worker — no blocking.
void stocksFetchTask() {
    TaskHandle_t worker = nullptr;
    uint32_t lastOk = 0;
    bool active = false;
    bool hasPriority = false;
    bool burstActive = false;
    if (lockStocks()) {
        worker = s_workerHandle;
        active = s_active;
        lastOk = s_lastOkMs;
        hasPriority = s_priorityIdx != 0xFF;
        burstActive = s_burstPending != 0;
        unlockStocks();
    } else {
        worker = s_workerHandle;
        lastOk = s_lastOkMs;
    }
    if (!worker) return;
    if (!active) return;
    uint32_t nowMs = millis();
    bool regularFresh = !hasPriority && !burstActive &&
                        lastOk > 0 && (nowMs - lastOk) < STOCKS_REGULAR_REFRESH_MIN_MS;
    if (regularFresh) return;
    bool stalled = lastOk > 0 && (nowMs - lastOk) > 300000UL;
    apiStop();
    delay(100);
    if (stalled) {
        Serial.printf("[Stocks] stall detected (%lus since last OK) — forcing burst\n",
                      (unsigned long)((nowMs - lastOk) / 1000));
        stocksRequestBurst();
        return;
    }
    xTaskNotifyGive(worker);
}

// Kick a burst refresh — fetches every watchlist symbol back-to-back
// (one RR step per fetch). Called when the user first enters Stocks mode
// so all charts populate within ~N×1-3s instead of N×60s. Kicks off with
// the currently focused symbol so the user sees *their* chart fill in on
// the first fetch (~2-3s) instead of waiting for the RR cursor to reach
// it after 2-5 earlier fetches.
void stocksRequestBurst() {
    TaskHandle_t worker = nullptr;
    ensureStocksWorkerRunning();
    if (!lockStocks(pdMS_TO_TICKS(500))) return;
    worker = s_workerHandle;
    if (!worker || !s_active || s_watchlist.count == 0) {
        unlockStocks();
        return;
    }
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
    memset(s_retryAtMs, 0, sizeof(s_retryAtMs));
    Serial.printf("[Stocks] burst refresh requested (pending=0x%02X focus=%u)\n",
                  (unsigned)mask, (unsigned)s_focusedIdx);
    unlockStocks();
    xTaskNotifyGive(worker);
}

bool stocksIsFetching() {
    if (!lockStocks()) return s_fetching;
    bool fetching = s_fetching;
    unlockStocks();
    return fetching;
}

const char* stocksLastDebug() {
    static char dbg[64];
    if (!lockStocks()) return s_lastDbg;
    strncpy(dbg, s_lastDbg, sizeof(dbg) - 1);
    dbg[sizeof(dbg) - 1] = '\0';
    unlockStocks();
    return dbg;
}

const char* stocksGetFocusedStatusText() {
    if (!lockStocks()) return nullptr;
    const char* status = formatStatusTextLocked(s_focusedIdx, s_statusBuf, sizeof(s_statusBuf));
    unlockStocks();
    return status;
}

bool stocksConsumeDirty() {
    if (!lockStocks()) return false;
    bool dirty = s_dirty;
    s_dirty = false;
    unlockStocks();
    return dirty;
}

void stocksStop() {
    TaskHandle_t h = nullptr;
    if (lockStocks(portMAX_DELAY)) {
        h = s_workerHandle;
        s_workerHandle = nullptr;
        s_fetching = false;
        s_burstPending = 0;
        unlockStocks();
    }
    if (h) {
        vTaskDelete(h);
        Serial.println("[Stocks] worker stopped");
    }
    // Release the worker's dedicated TLS session too — frees ~30KB DRAM.
    stocksClientStop();
}

uint8_t stocksGetFocusedIdx() {
    if (!lockStocks()) return s_focusedIdx;
    uint8_t idx = s_focusedIdx;
    unlockStocks();
    return idx;
}

uint8_t stocksGetWatchlistCount() {
    if (!lockStocks()) return s_watchlist.count;
    uint8_t count = s_watchlist.count;
    unlockStocks();
    return count;
}

const char* stocksGetFocusedSymbol() {
    static char symbol[STOCK_SYMBOL_LEN];
    if (!lockStocks()) return nullptr;
    if (s_watchlist.count == 0 || s_focusedIdx >= s_watchlist.count) {
        unlockStocks();
        return nullptr;
    }
    strncpy(symbol, s_watchlist.symbols[s_focusedIdx], sizeof(symbol) - 1);
    symbol[sizeof(symbol) - 1] = '\0';
    unlockStocks();
    return symbol;
}

const StockQuote* stocksGetFocusedQuote() {
    static StockQuote quote;
    if (!lockStocks()) return nullptr;
    if (s_watchlist.count == 0 || s_focusedIdx >= s_watchlist.count) {
        unlockStocks();
        return nullptr;
    }
    uint8_t slot = findQuoteSlot(s_watchlist.symbols[s_focusedIdx]);
    if (slot == 0xFF) {
        unlockStocks();
        return nullptr;
    }
    quote = s_quotes[slot];
    unlockStocks();
    return &quote;
}

const SparklineData* stocksGetFocusedSpark() {
    static SparklineData spark;
    if (!lockStocks()) return nullptr;
    if (s_watchlist.count == 0 || s_focusedIdx >= s_watchlist.count || !s_sparks[s_focusedIdx].valid) {
        unlockStocks();
        return nullptr;
    }
    spark = s_sparks[s_focusedIdx];
    unlockStocks();
    return &spark;
}

bool stocksGetFocusedSnapshot(StockFocusedSnapshot& out) {
    if (!lockStocks()) return false;
    out = {};
    out.focusedIdx = s_focusedIdx;
    out.watchlistCount = s_watchlist.count;
    out.fetching = s_fetching;

    if (s_watchlist.count == 0 || s_focusedIdx >= s_watchlist.count) {
        unlockStocks();
        return true;
    }

    strncpy(out.symbol, s_watchlist.symbols[s_focusedIdx], sizeof(out.symbol) - 1);
    out.symbol[sizeof(out.symbol) - 1] = '\0';
    formatStatusTextLocked(s_focusedIdx, out.status, sizeof(out.status));

    uint8_t slot = findQuoteSlot(out.symbol);
    if (slot != 0xFF) {
        out.quote = s_quotes[slot];
        out.hasQuote = out.quote.valid;
    }

    if (s_sparks[s_focusedIdx].valid) {
        out.spark = s_sparks[s_focusedIdx];
        out.hasSpark = out.spark.count >= 2;
    }

    unlockStocks();
    return true;
}

bool stocksGetSnapshotAt(uint8_t watchlistIndex, StockFocusedSnapshot& out) {
    if (!lockStocks()) return false;
    out = {};
    out.focusedIdx = watchlistIndex;
    out.watchlistCount = s_watchlist.count;
    out.fetching = s_fetching;

    if (watchlistIndex >= s_watchlist.count) {
        unlockStocks();
        return true;
    }

    strncpy(out.symbol, s_watchlist.symbols[watchlistIndex], sizeof(out.symbol) - 1);
    out.symbol[sizeof(out.symbol) - 1] = '\0';
    formatStatusTextLocked(watchlistIndex, out.status, sizeof(out.status));

    uint8_t quoteSlot = findQuoteSlot(out.symbol);
    if (quoteSlot != 0xFF) {
        out.quote = s_quotes[quoteSlot];
        out.hasQuote = out.quote.valid;
    }
    if (s_sparks[watchlistIndex].valid) {
        out.spark = s_sparks[watchlistIndex];
        out.hasSpark = out.spark.count >= 2;
    }

    unlockStocks();
    return true;
}

void stocksAdvanceFocused() {
    bool request = false;
    if (lockStocks()) {
        if (s_watchlist.count > 1) {
            s_focusedIdx = (s_focusedIdx + 1) % s_watchlist.count;
            s_priorityIdx = s_focusedIdx;
            s_dirty = true;
            request = true;
        }
        unlockStocks();
    }
    if (request) scheduler.requestRun(taskStocks);
}

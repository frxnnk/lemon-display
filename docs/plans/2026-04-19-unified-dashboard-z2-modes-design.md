# Unified Dashboard — Z2 Modes (USD / Markets / Stocks)

Date: 2026-04-19
Status: Design validated, ready for implementation

## Motivation

Two issues with v5.0.2:

1. **Stocks view feels isolated.** Switching to Stocks loses all crypto context (BTC hero, lemon-dollar, markets, clock). Heavy context switch for what is conceptually "another ticker".
2. **Only the focused ticker loads.** `stocksFetchTask` fetches one symbol per 60s tick — the focused one. Other tickers stay in "Loading…" until the user swipes to them.

## Concept

Collapse the two-view model (Crypto dashboard / Stocks fullscreen) into a **single dashboard with a modal Z2 slot**:

- **Z0**: header (unchanged)
- **Z1**: BTC hero (always present, same as today)
- **Z2**: modal slot with 3 modes
  - `Z2_USD` — Lemon Dollar (current default)
  - `Z2_MARKETS` — Polymarket (current prediction mode, promoted)
  - `Z2_STOCKS` — compact stocks card with internal ticker navigation

Layout 0 (BTC fullscreen) remains as "focus mode" — no Z2.

## Gestures

| Zone | Gesture | Action |
|------|---------|--------|
| Z1 (BTC hero) | double-tap | toggle layout 0 ↔ layout 1 (focus mode) |
| Z2 | swipe horizontal | cycle Z2 mode (USD → Markets → Stocks → USD) |
| Z2 (USD mode) | tap | toggle period (existing behavior) |
| Z2 (Markets mode) | tap | next market (existing behavior) |
| Z2 (Stocks mode) | tap | next ticker in watchlist |

Rationale: swipe = change context, tap = local action. No gesture conflicts.

## Data strategy (Stocks)

**Round-robin fetch + persistent cache.**

- Scheduler stocks tick (60s) rotates through watchlist — one symbol per tick.
- `s_quotes[]` is persisted to NVS as a single blob (debounced, ~1 write per full round-robin loop).
- On boot: load cache → Stocks mode shows stale-but-visible data instantly.
- Tap to advance ticker: paint from cache immediately + `scheduler.requestRun(taskStocks)` prioritized refresh of that symbol.
- Footer shows `Xm ago` per-symbol staleness indicator.
- N tickers → each fresh every N·60s. Acceptable for stocks (not HFT).

## Compact Stocks card layout (480×213)

```
┌─────────────────────────────────────────────┐
│ AAPL  Apple Inc.              $187.42       │
│                               +1.24 +0.67%  │
├─────────────────────────────────────────────┤
│   sparkline (full-width, ~100px tall)       │
├─────────────────────────────────────────────┤
│ H $188.12   L $185.90   1/5   · 2m ago      │
└─────────────────────────────────────────────┘
```

Z2-mode dots indicator: 3 pills top-right in Z2 showing active mode.

## State

```cpp
enum Z2Mode { Z2_USD = 0, Z2_MARKETS = 1, Z2_STOCKS = 2, Z2_COUNT = 3 };
static Z2Mode s_z2Mode = Z2_USD;       // persisted in NVS
static uint8_t s_stocksFocusedIdx;     // user-controlled via tap
static uint8_t s_rrIdx;                // round-robin cursor (independent)
```

## Removals

- `ui_views.cpp` / `ui_views.h` — the layer is gone.
- `dashboardSetMuted` / `s_muted` — no more competing views.
- `viewsDrawDotsOverlay` — replaced by Z2-mode dots.
- `ui_stocks.cpp` fullscreen sprite (`s_spr`, `ensureSprite`, full `stocksDrawAll` render). Keep fetch, NVS, data.

## Implementation phases

1. **Foundations** — `Z2Mode` enum, NVS persist for mode + quotes blob, round-robin in `stocksFetchTask`.
2. **Render in Z2** — `dashboardDrawStocksZ2`, dispatch in `dashboardRedrawZ2`.
3. **Gestures** — swipe cycles mode, tap advances ticker, mode-dots indicator.
4. **Cleanup** — delete `ui_views.*`, mute logic, `stocksDrawAll` fullscreen.
5. **Polish** — `Xm ago` timestamp, stale/error indicators.

## Risks

- **NVS corruption during blob write**: schema version + magic number; invalidate on mismatch.
- **sprZ2 re-creation on layout change**: Stocks render must be idempotent post-`createSprite`.
- **Double-tap semantics change**: previously cycled views. Now toggles layout 0/1 on Z1. Document in commit message.

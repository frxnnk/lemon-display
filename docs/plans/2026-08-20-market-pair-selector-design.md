# Market Pair Selector Design

## Goal

Make the two Markets cards visually identifiable and interactive. `USDT / ARS`
and `USDT / USD` act as selectors for the full-width seven-day chart below.

## Interaction

- The selected card uses the green border and an `ACTIVO`/`ACTIVE` marker.
- Each card shows the Tether icon plus the flag of its quote currency.
- Tapping a Markets card changes only the selected pair and redraws immediately.
- Navigation gestures and the bottom bar keep their existing behavior.
- ARS remains selected after boot and after returning to Markets.

## Data flow

The existing ARS chart remains sourced from CoinGecko's seven-day hourly market
chart. A second endpoint with `vs_currency=usd` is fetched in the same background
worker job. The parser receives pair-specific bounds, writes into a temporary
buffer, and only replaces the stored series after the whole response validates.
ARS variation values continue to come only from the ARS response.

The USD request uses the same 30-minute refresh and one-minute failure retry as
the ARS chart. Its validity and timestamps are independent, so an unavailable USD
series cannot invalidate ARS data or block touch handling.

## Rendering and failures

One generic chart renderer receives the selected series, count, freshness,
currency format, and title. It shows loading animation while the selected series
is being fetched, `--` when unavailable, and never displays data older than two
hours. The USD graph uses four decimals so peg movement remains visible.

## Verification

Tests cover endpoint configuration, independent USD state, card hitboxes,
selection behavior, icons, active-card styling, pair-specific chart rendering,
and stale-data hiding. Both firmware environments must compile before OTA
publication.

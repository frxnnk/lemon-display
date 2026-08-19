# V2 market readability

## Goal

Improve the readability of the always-on market screen without adding requests,
animations, or larger redraw regions.

## Decisions

- Show the primary stock price as a whole dollar value on Home and Context.
- Keep daily percentage change and intraday range precision unchanged.
- Render the header clock with `SatoshiMedium18` at the same horizontal center.
- Expand only the clock's clipped redraw region so updates remain synchronized
  and do not invalidate the full scene.
- Keep the default BTC/USD card identified by the BTC icon alone.
- For alternative BTC pairs, draw `VS ETH`, `VS SOL`, `VS ARS`, or `VS ORO`
  immediately to the right of the BTC icon.

## Constraints

- Reuse the existing `PairDef::label`; no new data source or persisted setting.
- Preserve the existing card dimensions, touch target, refresh cadence, and
  one-clip-per-VSync display strategy.
- Keep `firmware-v2.bin` as the OTA asset and do not flash over USB.

## Verification

- Regression coverage for whole-dollar stock prices, percentage precision,
  prominent clock rendering, clipped clock updates, and conditional pair labels.
- Full Python suite and `matouch_esp32s3_40_v2_real` PlatformIO build.
- Remote release asset download and MD5/SHA-256 comparison.

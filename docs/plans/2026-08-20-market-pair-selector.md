# Market Pair Selector Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add icon-backed selectable USDT/ARS and USDT/USD cards that switch the seven-day Markets chart.

**Architecture:** Keep selection in `UsdtRuntimeModel`, process Markets-card taps before general navigation, and fetch USD history in the existing background worker. Store ARS and USD series independently and feed either series to one generic chart renderer.

**Tech Stack:** ESP32 Arduino C++, LovyanGFX, ArduinoJson, PlatformIO, Python unittest contract tests.

---

### Task 1: Define pair selection and touch behavior

**Files:**
- Modify: `src/usdt_lemon_model.h`
- Test: `tools/test_usdt_firmware.py`

1. Add failing tests for the Markets card hitboxes and selected-pair state.
2. Run the focused tests and confirm they fail because the pair API is absent.
3. Add `UsdtMarketPair`, default ARS selection, card geometry, and a helper that changes the pair only for taps inside Markets cards.
4. Run focused tests and confirm they pass.

### Task 2: Add independent USD historical data

**Files:**
- Modify: `src/config.h`
- Modify: `src/usdt_lemon_data.h`
- Modify: `src/usdt_lemon_data.cpp`
- Test: `tools/test_usdt_firmware.py`

1. Add failing tests for the USD endpoint and independent USD chart validity, count, and timestamps.
2. Run focused tests and confirm the missing endpoint/state causes failure.
3. Generalize the market-chart parser with pair-specific validation bounds and add a USD-only fetch that does not alter ARS variations.
4. Preserve valid cached USD data when the request or parse fails.
5. Run focused tests and confirm they pass.

### Task 3: Render pair icons, active cards, and selected chart

**Files:**
- Modify: `src/usdt_lemon_ui.cpp`
- Test: `tools/test_usdt_firmware.py`

1. Add failing tests for Tether/fiat icons, active styling, and pair-specific graph selection.
2. Run focused tests and confirm the rendering contract is absent.
3. Add a United States flag icon and a dedicated Markets pair-card renderer.
4. Generalize the chart renderer for ARS and USD labels and numeric precision.
5. Run focused tests and confirm they pass.

### Task 4: Wire runtime touch and background updates

**Files:**
- Modify: `src/usdt_lemon_runtime.cpp`
- Test: `tools/test_usdt_firmware.py`

1. Add a failing test proving Markets taps are handled before generic gestures and trigger redraw without a network call.
2. Run the test and confirm it fails.
3. Add a thin Markets control handler and keep all requests in the worker.
4. Run the focused and complete test suites.

### Task 5: Version, build, review, and publish OTA

**Files:**
- Modify: `src/config.h`
- Modify: `docs/USDT_OTA.md`
- Modify: `firmware-usdt.bin`

1. Bump to `5.1.1-usdt.19` and update OTA docs.
2. Compile `matouch_esp32s3_40_usdt` and `matouch_esp32s3_40`.
3. Run all tests and `git diff --check`.
4. Review the source diff and address findings.
5. Rebuild the exact reviewed binary, commit it, push `codex/lemon-usdt-ota`, and publish `v5.1.1-usdt.19` as Latest.
6. Download the release asset and compare size, MD5, and SHA256 with the local binary.

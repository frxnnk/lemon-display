# Regions Card Alignment Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Align country labels, values, flags, and currencies consistently inside every Regions card.

**Architecture:** Add one region-specific renderer used by the four existing cards. Keep the existing flag functions and data model unchanged.

**Tech Stack:** ESP32 Arduino C++, LovyanGFX, PlatformIO, Python unittest contract tests.

---

### Task 1: Lock the regional card geometry

**Files:**
- Modify: `tools/test_usdt_firmware.py`
- Modify: `src/usdt_lemon_ui.cpp`

1. Add a failing contract test for `drawRegionCard`, shared value/flag baselines, and four renderer calls.
2. Run the focused test and confirm it fails because the renderer is absent.
3. Implement the renderer and replace detached `drawCard`/flag calls.
4. Run the focused and complete suites.
5. Commit the source change.

### Task 2: Publish the OTA build

**Files:**
- Modify: `src/config.h`
- Modify: `docs/USDT_OTA.md`
- Modify: `firmware-usdt.bin`

1. Bump the firmware to `5.1.1-usdt.20` with a failing version test first.
2. Build the USDT and base environments.
3. Review the diff and run all tests.
4. Generate and verify the exact OTA binary.
5. Push `codex/lemon-usdt-ota` and publish `v5.1.1-usdt.20` as Latest.

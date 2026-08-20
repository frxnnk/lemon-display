# USDt UI and Settings Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use executing-plans to implement this plan task-by-task.

**Goal:** Deliver the `.16` USDt firmware UI cleanup, readable variation selector, four-card Home, improved navigation, and persistent Spanish/English plus sound settings.

**Architecture:** Extend the existing `UsdtRuntimeModel` with two settings flags and explicit System hitboxes. Persist language beside the existing sound preference in NVS, route all USDt copy through a two-language selector, and keep drawing and interaction in the existing UI/runtime modules. Reuse live Lemon bid/ask for Home spread and the existing audio manager for touch feedback.

**Tech Stack:** ESP32 Arduino C++, LovyanGFX, Preferences/NVS, existing I2S audio manager, Python contract tests, PlatformIO.

---

### Task 1: Lock the visual contract

**Files:**
- Modify: `tools/test_usdt_firmware.py`

1. Add tests for removed footer copy, centered title group, vector navigation icons, four Home cards, and prominent 1H/24H/7D indicators.
2. Run the focused tests and confirm they fail because the old UI is still present.
3. Do not modify production code until the failures match the requested behavior.

### Task 2: Lock settings and localization behavior

**Files:**
- Modify: `tools/test_usdt_firmware.py`
- Modify: `src/usdt_lemon_model.h`
- Modify: `src/nvs_storage.h`
- Modify: `src/nvs_storage.cpp`

1. Add failing tests for language and sound hitboxes, NVS language persistence, audio initialization, and both ES/EN UI strings.
2. Run focused tests and confirm expected failures.
3. Add the minimal model flags, hit-test helpers, and NVS language getter/setter.
4. Run focused tests until green.

### Task 3: Implement the Home and shared chrome

**Files:**
- Modify: `src/usdt_lemon_ui.cpp`

1. Center logo-plus-title as one measured group.
2. Remove the three requested footer blocks.
3. Draw vector icons in navigation while preserving existing 96-pixel hit areas.
4. Replace the PEG sparkline with compact PEG and Spread cards.
5. Give Variation a prominent three-period selector.
6. Run focused UI tests until green.

### Task 4: Implement interactive System settings

**Files:**
- Modify: `src/usdt_lemon_ui.h`
- Modify: `src/usdt_lemon_ui.cpp`
- Modify: `src/usdt_lemon_runtime.cpp`
- Modify: `src/main.cpp`

1. Load persisted settings at USDt startup and initialize audio.
2. Handle sound, language, and Wi-Fi rows before normal navigation gestures.
3. Persist changes and redraw immediately; play feedback only when sound is enabled.
4. Localize loading, navigation, screen, status, and System copy.
5. Run focused settings tests until green.

### Task 5: Release verification

**Files:**
- Modify: `src/config.h`
- Modify: `docs/USDT_OTA.md`
- Modify: `tools/test_usdt_firmware.py`
- Modify: `firmware-usdt.bin`

1. Set `APP_VERSION` to `5.1.1-usdt.16` and update documentation.
2. Run `python -m unittest discover -s tools -p "test_*.py"` and require zero failures.
3. Run `python -m platformio run -e matouch_esp32s3_40_usdt` and require success.
4. Inspect the diff for unrelated changes, then commit source.
5. Build from a clean commit, copy the exact firmware binary, record MD5/SHA-256, and commit the artifact.
6. Push the branch and create `.16` as a draft release. Keep `.14` as Latest until the physical device version is confirmed.

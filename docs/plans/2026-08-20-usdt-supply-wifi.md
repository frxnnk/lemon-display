# USDt Supply and Wi-Fi Recovery Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace Spread with global USDt supply and make saved Wi-Fi reconnect after an unsuccessful boot connection.

**Architecture:** Extend the existing DefiLlama proxy contract with one validated global field, carry it through `UsdtNetworkData`, and render it with existing freshness behavior. Remove the first-connection retry gate from the shared Wi-Fi manager while retaining exponential backoff.

**Tech Stack:** ESP32 Arduino C++, FreeRTOS, ArduinoJson, Node.js Vercel function, Python unittest, PlatformIO.

---

### Task 1: Lock the proxy and firmware contracts

**Files:**
- Modify: `tools/test_usdt_firmware.py`
- Modify: `tools/test_runtime_stability.py`

1. Change the proxy fixture to include `circulating.peggedUSD` and require `totalSupplyUsd`.
2. Require the firmware parser/model and Overview to use global supply and remove Spread.
3. Require `wifiLoop()` to retry stored credentials without an `everConnected` gate.
4. Run the targeted tests and confirm they fail for the missing behavior.

### Task 2: Extend the global supply data flow

**Files:**
- Modify: `api/usdt-networks.js`
- Modify: `src/usdt_lemon_data.h`
- Modify: `src/usdt_lemon_data.cpp`
- Modify: `src/usdt_lemon_ui.cpp`

1. Validate and return `tether.circulating.peggedUSD` as `totalSupplyUsd`.
2. Parse it into `UsdtNetworkData.totalSupplyUsd`.
3. Render `SUPPLY USDt` with `formatUsdSupply` and the existing loading pulse.
4. Run the targeted tests and confirm they pass.

### Task 3: Repair first-boot Wi-Fi recovery

**Files:**
- Modify: `src/wifi_manager.cpp`

1. Preserve stored credentials after the synchronous boot timeout.
2. Let the existing backoff reconnect whenever saved credentials are present.
3. Run the targeted Wi-Fi regression and full suite.

### Task 4: Build and publish 5.1.1-usdt.22

**Files:**
- Modify: `src/config.h`
- Modify: `docs/USDT_OTA.md`
- Modify: `firmware-usdt.bin`

1. Bump the version test and confirm it fails, then bump `APP_VERSION` and docs.
2. Build `matouch_esp32s3_40_usdt`, copy the binary, and verify MD5/SHA256.
3. Deploy and verify the production proxy response.
4. Run the complete suite and rebuild from the committed tree.
5. Push `codex/lemon-usdt-ota`, publish the GitHub release, download its asset, and compare hashes.

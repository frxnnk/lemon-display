# Lemon Box Real Wireless Sync Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make Lemon Box Studio real end-to-end: live data sources, real device discovery, wireless settings sync, wireless normal OTA, and a firmware card runtime path so designed cards can run on the box without USB.

**Architecture:** Keep Studio local-first as the authoring/control surface. The Lemon Box becomes a paired LAN device with authenticated JSON APIs, mDNS discovery, live settings/watchlist sync, source health, and OTA upload/trigger endpoints. Arbitrary designs become real in two stages: first a declarative firmware card runtime for safe built-in card types, then optional generated firmware builds for advanced custom cards.

**Tech Stack:** Python stdlib HTTP backend, existing Studio job runner, ESP32-S3 Arduino firmware, ESPAsyncWebServer, ArduinoJson, NVS, existing OTA/PlatformIO/flasher tooling, local MCP stdio.

---

## Product Truth Table

**Make real now:**
- Crypto source preview using CoinGecko/Binance HTTP.
- Stocks/watchlist preview using Yahoo Finance-compatible query path.
- Weather using Open-Meteo by default, optional keyed providers later.
- Source health, caching, timeout, errors, and last payload.
- Device discovery over mDNS + manual IP fallback.
- Pairing token so Studio can apply settings without exposing secrets.
- Wireless sync for theme, layout, brightness, z2Mode, watchlist.
- Wireless normal OTA for local build or latest release.

**Make real after firmware runtime:**
- Cards beyond current BTC/USD/watchlist become actual on-device declarative cards.
- Custom HTTP JSON cards run from a constrained schema, not arbitrary code.

**Keep USB-only:**
- First install if the box does not yet have sync firmware.
- Recovery erase/write of bootloader, partitions, boot_app0, firmware, SPIFFS.
- Any state where the device is in `DOWNLOAD(USB/UART0)` or not running app firmware.

---

## Task 1: Define the Device Sync Contract

**Files:**
- Create: `docs/LEMON_BOX_SYNC_API.md`
- Modify: `studio/tool_registry.py`
- Test: `tools/test_studio_tool_registry.py`

**Step 1: Write the API contract doc**

Document these endpoints:

```http
GET  /api/device
GET  /api/health
GET  /api/settings
POST /api/settings
GET  /api/watchlist
POST /api/watchlist
POST /api/pair/start
POST /api/pair/confirm
POST /api/sync/experience
GET  /api/sync/experience
POST /api/ota/arm
POST /api/ota/upload
POST /api/ota/github
GET  /api/ota/status
```

Document that every mutating endpoint after pairing requires:

```http
Authorization: Bearer <local_pairing_token>
X-Lemon-Studio-Origin: localhost
```

**Step 2: Add capability fields**

Update `StudioToolRegistry.capabilities()` to include:

```python
"wireless": {
    "discovery": ["mdns", "manual_ip"],
    "sync": ["settings", "watchlist", "declarative_experience"],
    "ota": ["local_upload", "github_release"],
    "recovery": "usb_only"
}
```

**Step 3: Test**

Run:

```bash
python -m unittest tools.test_studio_tool_registry
```

Expected: PASS.

---

## Task 2: Add Device Identity, Health, and Pairing in Firmware

**Files:**
- Modify: `src/config_server.cpp`
- Modify: `src/config_server.h`
- Modify: `src/nvs_storage.h`
- Modify: `src/nvs_storage.cpp`
- Test: `tools/test_studio_firmware_api.py`

**Step 1: Write failing firmware static tests**

Assert `src/config_server.cpp` contains:

```cpp
"/api/device"
"/api/health"
"/api/pair/start"
"/api/pair/confirm"
"nvsGetPairingToken"
"nvsSetPairingToken"
```

**Step 2: Add NVS pairing functions**

Add:

```cpp
bool nvsGetPairingToken(char* out, size_t len);
void nvsSetPairingToken(const char* token);
void nvsClearPairingToken();
```

Store token under a short NVS key like `pair_tok`.

**Step 3: Implement device identity**

`GET /api/device` returns:

```json
{
  "ok": true,
  "name": "Lemon Box",
  "appVersion": "5.1.1-beta.38",
  "deviceId": "esp32s3-<chipid>",
  "paired": true,
  "capabilities": {
    "settings": true,
    "watchlist": true,
    "experienceSync": false,
    "otaUpload": false
  }
}
```

**Step 4: Implement health**

`GET /api/health` returns WiFi RSSI, uptime, free heap, app version, current settings, and whether OTA is busy.

**Step 5: Implement pairing**

Pairing flow:
- Studio calls `POST /api/pair/start`.
- Device generates 6-digit code + pending token, stores it in RAM for 120 seconds.
- Device renders or logs the code; first pass can expose it in serial and response while UI display is added.
- Studio calls `POST /api/pair/confirm` with code.
- Firmware stores token in NVS and returns token once.

**Step 6: Compile**

Run:

```bash
python -m platformio run -e matouch_esp32s3_40
```

Expected: SUCCESS.

---

## Task 3: Add mDNS Discovery and Device Registry in Studio

**Files:**
- Create: `studio/device_registry.py`
- Modify: `studio/device.py`
- Modify: `studio/server.py`
- Modify: `studio/static/app.js`
- Test: `tools/test_studio_server.py`

**Step 1: Write failing tests**

Add tests for:
- manual IP connect stores device profile,
- `/api/device/connect` calls `/api/device`,
- `/api/devices` returns known devices,
- failed health check becomes `offline` not an exception,
- token values are never returned to frontend.

**Step 2: Implement `DeviceRegistry`**

Persist to `studio/devices.local.json`:

```json
{
  "devices": [
    {
      "id": "esp32s3-123",
      "name": "Desk Lemon Box",
      "host": "192.168.1.42",
      "baseUrl": "http://192.168.1.42",
      "tokenRef": "DEVICE_TOKEN_esp32s3_123",
      "lastSeen": 1782952000,
      "status": "online"
    }
  ]
}
```

**Step 3: Add routes**

```http
GET  /api/devices
POST /api/devices/scan
POST /api/device/connect
POST /api/device/pair
GET  /api/device/health
```

For mDNS, prefer a small optional adapter. If adding a dependency, justify it in the PR because Python stdlib does not include mDNS. If avoiding dependency in v1, implement manual IP first and leave scan as best-effort.

**Step 4: Update UI**

Device panel states:
- no device,
- found unpaired,
- pairing code required,
- online,
- offline,
- firmware too old,
- OTA available.

**Step 5: Test**

Run:

```bash
python -m unittest tools.test_studio_server tools.test_studio_static
```

Expected: PASS.

---

## Task 4: Replace Mock Source Adapters With Real HTTP Adapters

**Files:**
- Modify: `studio/sources.py`
- Create: `studio/http_client.py`
- Test: `tools/test_studio_sources.py`

**Step 1: Write failing tests**

Test each source with injected fake HTTP:
- `market.crypto` parses CoinGecko simple price or Binance ticker.
- `market.stocks` parses Yahoo quote response.
- `weather.current` parses Open-Meteo forecast.
- `custom.http_json` supports `jsonPath`, headers using key names, timeout, invalid JSON.
- no response contains secret values.

**Step 2: Add shared HTTP client**

Implement:

```python
class HttpClient:
    def get_json(self, url, headers=None, timeout=5):
        ...
```

Use stdlib `urllib.request`, bounded body size, and clear errors.

**Step 3: Implement adapters**

Real defaults:
- Crypto: CoinGecko `simple/price` for BTC/USD; fallback to Binance if configured.
- Stocks: Yahoo query endpoint used by firmware path.
- Weather: Open-Meteo geocoding + forecast, no key required.
- Custom HTTP: real today, improve auth/header injection by key name.

**Step 4: Add caching**

Cache in memory by source id:
- crypto: 15 seconds,
- stocks: 60 seconds,
- weather: 10 minutes,
- custom: source-configured TTL default 30 seconds.

**Step 5: Test**

Run:

```bash
python -m unittest tools.test_studio_sources
```

Expected: PASS.

---

## Task 5: Make Source Health Live in the UI

**Files:**
- Modify: `studio/static/app.js`
- Modify: `studio/static/screen_renderer.js`
- Modify: `studio/static/styles.css`
- Test: `tools/test_studio_static.py`

**Step 1: Add UI states**

Each source shows:
- live,
- cached,
- missing key,
- timed out,
- invalid payload,
- preview-only,
- device-supported.

**Step 2: Bind preview payloads to cards**

When source preview succeeds, attach `card.preview` and redraw:
- BTC card uses real price/change.
- Watchlist uses real rows.
- Weather uses real temp/condition.
- Custom uses selected JSON path.

**Step 3: Browser verify**

Run Studio and verify:
- preview button changes source state,
- card canvas changes,
- 3D screen changes,
- no overlap desktop/mobile.

---

## Task 6: Wireless Settings and Watchlist Sync

**Files:**
- Modify: `studio/device.py`
- Modify: `studio/server.py`
- Modify: `studio/static/app.js`
- Modify: `src/config_server.cpp`
- Test: `tools/test_studio_server.py`
- Test: `tools/test_studio_firmware_api.py`

**Step 1: Harden firmware settings endpoints**

Require bearer token for:

```http
POST /api/settings
POST /api/watchlist
```

Keep `GET` endpoints public or token-protected based on pairing state.

**Step 2: Add Studio sync job**

Add job:

```http
POST /api/jobs/sync-settings
```

Payload:

```json
{
  "deviceId": "esp32s3-123",
  "settings": {
    "theme": "dark",
    "layout": "btc_usd",
    "brightness": 220,
    "z2Mode": "stocks",
    "watchlist": ["AAPL", "TSLA"]
  }
}
```

**Step 3: UI**

Replace raw “Apply to device” with:
- device selector,
- sync diff,
- apply supported settings,
- status timeline,
- rollback hint.

**Step 4: Manual hardware acceptance**

With box on same WiFi:
- connect by IP,
- pair,
- change brightness,
- change theme,
- change watchlist,
- confirm screen updates without USB.

---

## Task 7: Wireless OTA for Normal Firmware Updates

**Files:**
- Modify: `src/ota_manager.cpp`
- Modify: `src/ota_manager.h`
- Modify: `src/config_server.cpp`
- Modify: `studio/services.py`
- Modify: `studio/server.py`
- Modify: `studio/static/app.js`
- Test: `tools/test_studio_server.py`
- Test: `tools/test_studio_firmware_api.py`
- Preserve: `tools/test_lemon_flasher.py`

**Step 1: Define OTA safety rules**

Normal wireless OTA:
- app partition only,
- no bootloader writes,
- no partition-table writes,
- MD5 required,
- version shown before update,
- explicit arm step,
- device reboots after successful update.

Recovery remains USB-only.

**Step 2: Firmware OTA endpoints**

Add:

```http
POST /api/ota/arm
POST /api/ota/upload
POST /api/ota/github
GET  /api/ota/status
```

Upload handler streams chunks into `Update.write()`.

**Step 3: Studio OTA jobs**

Add jobs:

```http
POST /api/jobs/ota-upload
POST /api/jobs/ota-latest
POST /api/jobs/ota-verify
```

**Step 4: UI**

Flash panel gets two lanes:
- Wireless OTA: paired device, app-only, restart/verify.
- USB Recovery: current Control Room recovery flow.

**Step 5: Test**

Run:

```bash
python -m unittest tools.test_studio_server tools.test_lemon_flasher
python -m platformio run -e matouch_esp32s3_40
```

Expected:
- server tests pass,
- flasher safety tests still pass,
- firmware compiles.

---

## Task 8: Declarative Firmware Card Runtime

**Files:**
- Create: `src/card_runtime.h`
- Create: `src/card_runtime.cpp`
- Modify: `src/ui_dashboard.cpp`
- Modify: `src/ui_dashboard.h`
- Modify: `src/config_server.cpp`
- Modify: `studio/manifest.py`
- Modify: `studio/static/screen_renderer.js`
- Test: `tools/test_studio_experience.py`
- Compile: PlatformIO.

**Step 1: Define supported card schema**

Initial real runtime:

```json
{
  "cards": [
    {
      "id": "weather",
      "kind": "metric",
      "title": "Weather",
      "source": "weather-current",
      "field": "tempC",
      "unit": "C",
      "x": 252,
      "y": 68,
      "w": 204,
      "h": 132,
      "style": {"accent": "sky"}
    }
  ]
}
```

Kinds:
- `metric`,
- `text`,
- `list`,
- `sparkline`,
- `btc`,
- `dollar`,
- `watchlist`.

**Step 2: Persist experience on device**

Use NVS for small settings and LittleFS/SPIFFS for full JSON if available. If filesystem is not configured, add a bounded NVS blob and reject oversized manifests.

**Step 3: Render cards**

Implement a small renderer that draws:
- title,
- source state,
- value/list/chart,
- preview-only fallback.

Do not add a generic JS-like runtime on ESP32.

**Step 4: Sync endpoint**

`POST /api/sync/experience` accepts declarative JSON only. It rejects:
- unknown kind,
- oversized manifest,
- secret-looking fields,
- unsupported source adapters.

**Step 5: Studio validation**

`validate_experience` changes from “preview-only” to:
- `device-supported` for declarative supported cards,
- `requires-firmware-build` for generated/C++ cards,
- `preview-only` for impossible/unsafe cards.

---

## Task 9: Real Agent/V0 Handoff Without Unsafe Device Actions

**Files:**
- Modify: `studio/tool_registry.py`
- Modify: `studio/mcp_server.py`
- Modify: `studio/services.py`
- Modify: `studio/static/app.js`
- Test: `tools/test_studio_tool_registry.py`
- Test: `tools/test_studio_mcp_server.py`

**Step 1: Expand draft tools**

Keep destructive actions absent. Add:

```python
"simulate_experience_on_device"
"export_declarative_runtime_bundle"
"explain_firmware_compatibility"
```

**Step 2: Optional provider integration**

If the user adds `OPENAI_API_KEY` or Anthropic key, Studio can call a model to propose an experience. Otherwise MCP handoff remains the default.

**Step 3: Permissions**

Agents can:
- read experience,
- preview sources,
- create drafts,
- validate compatibility,
- save drafts.

Agents cannot:
- flash,
- OTA,
- pair device,
- write secrets,
- run recovery.

---

## Task 10: End-to-End Browser and Hardware Acceptance

**Files:**
- Modify: `tools/test_studio_static.py`
- Create: `docs/HARDWARE_ACCEPTANCE.md`

**Automated checks**

Run:

```bash
python -m unittest tools.test_studio_experience tools.test_studio_sources tools.test_studio_tool_registry tools.test_studio_mcp_server tools.test_studio_server tools.test_studio_static tools.test_studio_firmware_api tools.test_studio_manifest tools.test_studio_services tools.test_studio_jobs tools.test_studio_keys tools.test_lemon_flasher
node --check studio/static/app.js
node --check studio/static/screen_renderer.js
node --check studio/static/workbench3d.js
python -m platformio run -e matouch_esp32s3_40
```

**Manual hardware acceptance**

1. Flash sync-capable firmware once over USB.
2. Connect Lemon Box to WiFi.
3. Studio discovers device or connects by IP.
4. Pair with code.
5. Apply brightness/theme/layout/watchlist without USB.
6. Preview real BTC/stocks/weather sources in Studio.
7. Sync declarative experience without USB.
8. Confirm the screen changes on-device.
9. Build firmware locally.
10. Run wireless normal OTA.
11. Confirm device reboots into new version.
12. Confirm recovery still requires USB and double confirmation.

---

## Recommended Milestones

**Milestone 1: Real Data + Real Device Settings**
- Tasks 1-6.
- Result: no cable needed for settings/watchlist/data previews.

**Milestone 2: Wireless OTA**
- Task 7.
- Result: no cable needed for normal firmware updates.

**Milestone 3: Real On-Device Cards**
- Task 8.
- Result: designed cards run on the physical box.

**Milestone 4: Agent Creator Loop**
- Task 9.
- Result: Codex/Claude can draft experiences safely; human still confirms device actions.


# Lemon Box — ESP32-S3 Firmware

Firmware for the Lemon Box: a 4.0" touchscreen crypto/finance desk device built on the MaTouch ESP32-S3. Tracks Bitcoin (multiple pairs and timeframes), Argentine dollar (USDT/ARS via Lemon Cash), US stocks, and Polymarket predictions. OTA updates via GitHub Releases.

## Hardware

| Component | Spec |
|-----------|------|
| Board | MaTouch ESP32-S3 4.0" (Makerfabs) |
| Display | 4.0" 480x480 IPS RGB, ST7701S controller |
| Touch | GT911 capacitive (I2C) |
| Audio | MAX98357A I2S amplifier |
| MCU | ESP32-S3, 16MB Flash (DIO), 8MB OPI PSRAM |

Full pin mapping: [docs/HARDWARE.md](docs/HARDWARE.md)

## Build Environments

| Env | Purpose |
|-----|---------|
| `matouch_esp32s3_40` | Production V1 dashboard (default) |
| `matouch_esp32s3_40_v2_demo` | V2 offline demo player (deterministic 78s loop) |
| `matouch_esp32s3_40_v2_real` | V2 real-time runtime (live data, OTA channel `firmware-v2.bin`) |
| `matouch_esp32s3_40_usdt` | USDT Control Room live (Lemon yield + CriptoYa + Coinbase + CoinGecko, OTA `firmware-usdt.bin`) |
| `matouch_esp32s3_40_usdt_mock` | USDT Control Room mock (offline, static validation data) |

## Quick Start

### Prerequisites

- Python 3.10+
- PlatformIO Core (`pip install platformio`)
- esptool (`pip install esptool`)

### 1. Clone and configure

```bash
git clone https://github.com/frxnnk/lemon-display.git
cd lemon-display
cp src/secrets.h.example src/secrets.h
# Edit src/secrets.h with your CoinGecko API key (free tier works)
```

### 2. Build

```bash
pio run -e matouch_esp32s3_40
```

### 3. Flash via USB

```bash
pio run -e matouch_esp32s3_40 -t upload
pio device monitor -b 115200
```

On first boot the device shows a QR code for WiFi provisioning.

### USDT Control Room mock

The mock is an offline visual-validation build. It does not call APIs, write
credentials, or perform trades. It boots directly into the USDT Control Room
and navigates Overview, Networks, Markets and Regions with
swipes or taps. The generated binary is:

```text
.pio/build/matouch_esp32s3_40_usdt_mock/firmware.bin
```

Build it with:

```bash
pio run -e matouch_esp32s3_40_usdt_mock
```

Flash only this mock binary at `0x10000` using the normal DIO-safe procedure;
do not use the older `firmware-v2.bin` asset for this validation.

## Flashing with esptool (manual / recovery)

**CRITICAL: Always use `--flash-mode keep --flash-size keep`.** Never force `qio` — the ESP32-S3 uses DIO and forcing QIO causes boot loops.

### Detect the port

ESP32-S3 USB VID is `0x303A`. On Windows, check Device Manager or:

```powershell
Get-WMIObject Win32_SerialPort | Where-Object { $_.PNPDeviceID -match "VID_303A" }
```

### Enter bootloader (if needed)

Hold BOOT → press RESET → release BOOT.

### Normal flash (device has working bootloader)

```bash
python -m esptool --chip esp32s3 --port COMX --baud 921600 \
  --before default-reset --after hard-reset \
  write-flash --flash-mode keep --flash-size keep \
  0x10000 firmware.bin
```

### Recovery flash (full erase + all partitions)

```bash
python -m esptool --chip esp32s3 --port COMX --baud 921600 \
  --before default-reset --after no-reset erase-flash

python -m esptool --chip esp32s3 --port COMX --baud 921600 \
  --before no-reset --after no-reset \
  write-flash --flash-mode keep --flash-size keep \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin \
  0xc90000 spiffs.bin
```

### Verify

```bash
python -m esptool --chip esp32s3 --port COMX --baud 921600 \
  --before no-reset --after hard-reset \
  verify-flash \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin \
  0xc90000 spiffs.bin
```

A stable USB port after reset = success. Port appearing/disappearing = boot loop (re-flash with recovery).

### GUI Flasher

`tools/lemon_flasher.py` provides a Tkinter GUI for flashing. The Codex skill lives in `skills/lemon-box-flasher/` (copy that folder into `~/.agents/skills/lemon-box-flasher` on another PC). Run tests with:

```bash
python -m unittest discover -s tools -p "test_*.py"
```

## OTA (Over-The-Air Updates)

The device auto-updates from GitHub Releases. No physical access needed.

### How it works

1. On boot, the device queries `https://api.github.com/repos/frxnnk/lemon-display/releases/latest`
2. Compares the release tag against `APP_VERSION` in `src/config.h` (semver with prerelease support)
3. Looks for a specific asset: `firmware.bin` (V1), `firmware-v2.bin` (V2 real mode) or `firmware-usdt.bin` (USDT mode)
4. Validates MD5 from the release body (format: `firmware-v2.bin MD5: <hash>`)
5. Downloads via GitHub CDN (follows 302 redirect) and flashes with `Update` library
6. Reboots on success

Additionally, a lightweight HEAD probe to `/releases/latest` runs periodically to detect new tags without hitting the rate-limited API.

### Publishing an OTA update

1. Build the firmware:
   ```bash
   pio run -e matouch_esp32s3_40_v2_real
   ```
2. The binary is at `.pio/build/matouch_esp32s3_40_v2_real/firmware.bin`
3. Compute MD5:
   ```bash
   certutil -hashfile firmware.bin MD5
   ```
4. Create a GitHub release on `frxnnk/lemon-display`:
   - Tag: `v5.1.1-beta.50` (must be newer than current `APP_VERSION`)
   - Asset: upload as `firmware-v2.bin` (or `firmware.bin` for V1)
   - Body must contain: `firmware-v2.bin MD5: <32-char-hash>`
5. Devices pick it up automatically within the probe interval

### Remote trigger (local network)

The config server exposes `POST /api/ota` which triggers check + flash immediately:

```bash
curl -X POST http://<device-ip>/api/ota -H "Content-Type: application/json" -d '{}'
```

### OTA channels

| Build env | Asset name | Channel isolation |
|-----------|-----------|-------------------|
| `matouch_esp32s3_40` | `firmware.bin` | V1 devices only |
| `matouch_esp32s3_40_v2_real` | `firmware-v2.bin` | V2 devices only |
| `matouch_esp32s3_40_usdt` | `firmware-usdt.bin` | USDT devices only |
| `matouch_esp32s3_40_v2_demo` | (no OTA) | Demo mode, no updates |

## Architecture

### V1 (default env)

Classic dashboard: BTC hero with sparkline, Lemon dollar card, stocks row, Polymarket predictions. Entry point: `src/main.cpp`.

### V2 (v2_real / v2_demo envs)

Redesigned runtime with scene-based navigation (home, data, settings). Entry point: `src/v2_runtime.cpp`.

- `v2_runtime.cpp` — Main loop, network services, OTA scheduling, scene management
- `ui_v2_runtime.cpp` — V2 home/data rendering
- `ui_v2_settings.cpp` — V2 settings screen
- `ui_v2_demo.cpp` — Offline demo player (v2_demo env only)

### Key modules

| File | Responsibility |
|------|---------------|
| `main.cpp` | V1 entry, state machine, scheduler, touch dispatch |
| `v2_runtime.cpp` | V2 entry, lifecycle, OTA, network orchestration |
| `config.h` | Version, endpoints, intervals, pin defs, period/pair tables |
| `display_manager.*` | LovyanGFX init, brightness, global `tft` |
| `wifi_manager.*` | WiFi connect/reconnect |
| `wifi_provision.*` | QR + captive portal provisioning |
| `api_client.*` | CoinGecko, CriptoYa, Yahoo Finance HTTP |
| `ws_binance.*` | Binance WebSocket (1m klines, real-time price) |
| `stocks_client.*` | Yahoo Finance stock quotes + charts |
| `ota_manager.*` | GitHub Releases check + download + flash |
| `scheduler.*` | Cooperative periodic task scheduler |
| `nvs_storage.*` | ESP32 NVS preferences (WiFi, settings, watchlist) |
| `config_server.*` | AsyncWebServer: captive portal + REST API + OTA trigger |
| `touch_manager.*` | GT911 polling, gestures (tap, long-press, swipe) |
| `audio_manager.*` | I2S tones and alerts |
| `time_manager.*` | NTP sync |
| `colors.*` | RGB565 palette (dark/light themes) |
| `design_system.h` | Typography, spacing, layout constants |
| `animation.h` | ValueAnimator, SparklineAnimator |

### Data sources

| Source | Data | Auth |
|--------|------|------|
| Binance WS | Real-time BTC price (1m klines) | None |
| Binance REST | Historical klines (4h+ periods) | None |
| CoinGecko | BTC changes, global stats, charts, multi-coin | API key (free) |
| CriptoYa | Lemon Cash USDT/ARS bid/ask | None |
| Coinbase | USDT/USD PEG and BRL/PEN/COP regional rates | None |
| Lemon API | Live USDt Lemon Yield APR | None |
| Yahoo Finance | Stock quotes + sparklines | None |
| Polymarket Gamma | Prediction markets | None |

## Project Structure

```
lemon-display/
├── src/                    # Firmware source (C++ / Arduino framework)
│   ├── main.cpp            # V1 entry point
│   ├── v2_runtime.*        # V2 entry point
│   ├── config.h            # Central configuration
│   ├── data/               # Embedded fonts + bitmaps
│   └── ...                 # Modules (see table above)
├── tools/
│   ├── lemon_flasher.py    # GUI flash tool (Tkinter)
│   ├── png_to_rgb565.py    # Asset converter
│   ├── ttf_to_gfx.py      # Font converter
│   ├── flash_v2_*.ps1      # Canary flash scripts
│   └── test_*.py           # Unit tests
├── docs/
│   ├── HARDWARE.md         # GPIO pin mapping
│   ├── SYSTEM_DESIGN.md    # V1 architecture deep-dive
│   ├── v2/                 # V2 product + architecture docs
│   └── plans/              # Implementation plans
├── landing/                # Product landing page (static)
├── studio/                 # Local dev tools (MCP server, etc.)
├── platformio.ini          # Build configuration
└── AGENTS.md               # LLM agent instructions
```

## Dependencies (PlatformIO)

| Library | Version | Purpose |
|---------|---------|---------|
| LovyanGFX | 1.1.16 | Display driver + graphics |
| ArduinoJson | ^7.3.0 | JSON parsing |
| QRCode | latest | WiFi provisioning QR |
| ESPAsyncWebServer | ^1.2.3 | Captive portal + REST API |
| AsyncTCP | ^1.1.1 | Async TCP transport |
| WebSockets | ^2.4.1 | Binance WS client |

## License

Proprietary. Brand assets are property of [Lemon](https://lemon.me).

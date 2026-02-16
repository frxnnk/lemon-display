# Lemon Display

Crypto dashboard running on a MaTouch ESP32-S3 4.0" touchscreen (480x480 RGB). Tracks Bitcoin price with live sparkline charts and Argentine Lemon dollar (USDT/ARS) rates, all wrapped in Lemon's official brand design system.

<!-- TODO: Add photo of the device -->

## Features

- **BTC Price Hero** — Large price display with 1h/24h/7d change badges and animated value transitions
- **Sparkline Chart** — Interactive chart with selectable periods (1h, 24h, 7d) and progressive reveal animation
- **Lemon Dollar** — Real-time USDT/ARS bid/ask from CriptoYa (Lemon Cash exchange)
- **Live Mode** — Double-tap to switch from 60s to 10s BTC polling
- **WiFi QR Provisioning** — Scan a QR code, connect to the device's AP, enter WiFi credentials via captive portal
- **Audio Alerts** — MAX98357A I2S audio: startup chime, BTC price movement alerts (>5% in 1h)
- **NVS Persistence** — Brightness, sound, alerts, and WiFi credentials survive reboots
- **Touch Gestures** — Tap to refresh, long-press header for settings, double-tap for live mode
- **Zone Rendering** — Sprite-based per-zone rendering with PSRAM double buffering (no tearing)

## Hardware

| Component | Spec |
|-----------|------|
| Board | MaTouch ESP32-S3 4.0" (Makerfabs) |
| Display | 4.0" 480x480 IPS RGB, ST7701S controller |
| Touch | GT911 capacitive (I2C) |
| Audio | MAX98357A I2S amplifier |
| MCU | ESP32-S3, 16MB Flash, 8MB OPI PSRAM |

See [docs/HARDWARE.md](docs/HARDWARE.md) for full pin mapping.

## Quick Start

### 1. Clone

```bash
git clone https://github.com/frxnnk/lemon-display.git
cd lemon-display
```

### 2. Create `src/secrets.h`

```bash
cp src/secrets.h.example src/secrets.h
```

Edit `src/secrets.h` and add your [CoinGecko Demo API key](https://www.coingecko.com/en/api/pricing) (free tier: 30 calls/min).

### 3. Build & Flash

```bash
# PlatformIO CLI
pio run
pio run -t upload
pio device monitor -b 115200
```

On first boot, the device shows a QR code for WiFi provisioning. Scan it with your phone, connect to the AP, and enter your home WiFi credentials.

## Project Structure

```
lemon-display/
├── src/
│   ├── main.cpp              # Entry point, state machine, scheduler setup
│   ├── config.h              # API endpoints, intervals, pins, version
│   ├── colors.h              # Lemon brand RGB565 palette
│   ├── design_system.h       # Typography, spacing, layout constants
│   ├── data_models.h         # BtcPrice, LemonPrice, SparklineData structs
│   ├── lgfx_matouch_40.h     # LovyanGFX display config (ST7701S + GT911)
│   ├── display_manager.*     # tft global, brightness control
│   ├── app_state.*           # Screen state machine (7 screens)
│   ├── wifi_manager.*        # WiFi connect/reconnect
│   ├── wifi_provision.*      # QR code + captive portal provisioning
│   ├── api_client.*          # CoinGecko + CriptoYa HTTP client
│   ├── time_manager.*        # NTP time sync
│   ├── touch_manager.*       # GT911 touch polling + gesture detection
│   ├── touch_utils.h         # Hit-test helpers
│   ├── scheduler.*           # Periodic task scheduler (4 tasks)
│   ├── animation.h           # ValueAnimator + SparklineAnimator
│   ├── audio_manager.*       # I2S tone generation + alert sounds
│   ├── nvs_storage.*         # ESP32 NVS preferences wrapper
│   ├── ui_dashboard.*        # Dashboard zones (header, BTC hero, lemon)
│   ├── ui_components.*       # GlassCard, sparkline, badges, toast, slider
│   ├── ui_settings.*         # Settings screen
│   └── data/                 # Embedded assets (fonts + bitmaps)
│       ├── satoshi_fonts.h   # Font bundle include
│       ├── Satoshi*.h        # Satoshi font variants (9, 12, 18, 24, 40pt)
│       └── lemon_*.h         # Brand bitmaps (logo, isotipo, imagotipo)
├── tools/
│   ├── png_to_rgb565.py      # PNG → RGB565 C header converter
│   ├── ttf_to_gfx.py        # TTF → LovyanGFX font header converter
│   ├── fonts/                # Source font files (Satoshi)
│   └── *.png                 # Source brand assets
├── docs/
│   ├── SYSTEM_DESIGN.md      # Architecture deep-dive
│   ├── HARDWARE.md           # Pin mapping + board config
│   └── lemon_brand_guidelines.md  # Color palette reference
├── data/                     # (reserved for SPIFFS/LittleFS, currently unused)
└── platformio.ini            # Build configuration
```

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | 1.1.16 | Display driver + graphics |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | ^7.3.0 | API response parsing |
| [QRCode](https://github.com/ricmoo/QRCode) | latest | WiFi provisioning QR generation |
| [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) | ^1.2.3 | Captive portal web server |
| [AsyncTCP](https://github.com/me-no-dev/AsyncTCP) | ^1.1.1 | Async TCP for web server |

## APIs Used

- **CoinGecko** — BTC price, market chart (sparkline), global dominance
- **CriptoYa** — Lemon Cash USDT/ARS bid/ask rates

## Architecture

See [docs/SYSTEM_DESIGN.md](docs/SYSTEM_DESIGN.md) for the full system architecture, including data flow, scheduler design, animation system, and WiFi provisioning flow.

## License

Proprietary. Brand assets and design guidelines are property of [Lemon](https://lemon.me).

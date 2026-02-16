# System Design

## Module Architecture

```
main.cpp
├── app_state         Screen state machine (7 screens)
├── scheduler         Periodic task runner (clock, BTC, sparkline, lemon)
├── animation         ValueAnimator + SparklineAnimator
│
├── wifi_manager      STA connect / reconnect
├── wifi_provision    AP + captive portal + QR code
├── time_manager      NTP sync (pool.ntp.org, UTC-3)
├── api_client        HTTP GET → JSON → data models
│
├── display_manager   LovyanGFX tft global, brightness
├── ui_dashboard      Zone-based dashboard rendering
├── ui_components     Reusable UI primitives
├── ui_settings       Settings screen
│
├── touch_manager     GT911 polling → gesture detection
├── audio_manager     I2S tone generation
└── nvs_storage       ESP32 Preferences wrapper
```

Hardware abstraction:

```
lgfx_matouch_40.h    Panel_ST7701 + Bus_RGB + Touch_GT911 + Light_PWM
config.h             Pin definitions, API URLs, intervals
colors.h             Lemon brand RGB565 palette
design_system.h      Typography, spacing, radii
data_models.h        BtcPrice, LemonPrice, SparklineData, etc.
```

## Screen State Machine

Defined in `app_state.h`. Seven states:

```
BOOT_SPLASH ──→ [has WiFi?]
                  ├─ yes → LOADING → DASHBOARD
                  └─ no  → WIFI_QR → WIFI_CONNECTING ──→ [connected?]
                                                          ├─ yes → LOADING → DASHBOARD
                                                          └─ no  → WIFI_FAILED
                                                                    ├─ retry    → LOADING
                                                                    └─ reconfig → WIFI_QR

DASHBOARD ──long press header──→ SETTINGS ──back──→ DASHBOARD
```

## Dashboard Zone Layout

Three rendering zones, each drawn to a PSRAM-backed sprite then pushed to the display:

```
┌──────────────────────────────────┐
│ Z0  Header (Y=0, H=44)          │  Clock + offline/live indicators
├──────────────────────────────────┤
│                                  │
│ Z1  BTC Hero (Y=48, H=300)      │  Price + glow + change badges
│                                  │  Sparkline chart + period selector
│                                  │
├──────────────────────────────────┤
│ Z2  Lemon Dollar (Y=354, H=120) │  USDT/ARS bid/ask + spread
│                                  │
└──────────────────────────────────┘
        480 x 480 pixels
```

Each zone is rendered to a sprite (`LGFX_Sprite`) allocated in PSRAM, then pushed with `pushSprite()`. This eliminates tearing since writes happen off-screen.

## Data Flow

```
WiFi ──→ NTP ──→ API Client ──→ JSON Parse ──→ Data Models
                                                    │
                                              Animator ──→ Zone Redraw ──→ Sprite ──→ Display
```

1. **WiFi connects** (STA mode, or provisioned via AP captive portal)
2. **NTP syncs** time from `pool.ntp.org` (GMT-3 for Argentina)
3. **API client** fetches JSON from CoinGecko / CriptoYa
4. **Data models** (`BtcPrice`, `LemonPrice`, `SparklineData`) hold parsed data
5. **Animators** interpolate between old and new values (ease-out cubic, 500ms)
6. **Zone redraw** renders the affected zone sprite
7. **Sprite push** sends the sprite to the display (no tearing)

## API Endpoints

| Endpoint | Source | Interval | Data |
|----------|--------|----------|------|
| `/simple/price` | CoinGecko | 60s (10s live) | BTC/USD + 1h/24h/7d change |
| `/coins/bitcoin/market_chart` | CoinGecko | 15min | Sparkline price history |
| `/lemoncash/usdt/ars` | CriptoYa | 30s | USDT/ARS bid + ask |

All requests include the CoinGecko demo API key as `x_cg_demo_api_key` header.

## Scheduler

Four periodic tasks managed by `Scheduler` (max 8 slots):

| Task | Interval | Callback |
|------|----------|----------|
| `clock` | 1s | Update header time display |
| `btc` | 60s | Fetch BTC price, trigger animation |
| `sparkline` | 15min | Fetch sparkline chart data |
| `lemon` | 30s | Fetch USDT/ARS rates |

Tasks can be force-triggered via `scheduler.forceRun(taskId)` (e.g., on touch tap). In live mode, BTC polling bypasses the scheduler with a 10s manual timer.

## Animation System

Two animator classes in `animation.h`:

### ValueAnimator
Smooth numeric transitions (prices). Uses ease-out cubic: `1 - (1-t)^3` over 500ms.

```
setTarget(newPrice)  →  animates current → target over 500ms
update()             →  returns interpolated value each frame
set(val)             →  instant set (no animation, for init)
```

### SparklineAnimator
Progressive chart reveal. Uses ease-out quadratic over 800ms. `visibleCount()` increases from 1 to `data.count`, used by the sparkline renderer to draw points incrementally.

### Frame Throttle
Animation updates are throttled to ~30fps (33ms interval) in the main loop to reduce display tearing.

## Touch System

`touch_manager` polls GT911 via I2C and produces `TouchEvent` structs:

| Gesture | Detection |
|---------|-----------|
| `TOUCH_TAP` | Press < 500ms, movement < 20px |
| `TOUCH_LONG_PRESS` | Press > 800ms |
| `TOUCH_SWIPE_LEFT` | Horizontal drag > 50px leftward |
| `TOUCH_SWIPE_RIGHT` | Horizontal drag > 50px rightward |

Dashboard touch dispatching:
- **Z0 tap**: Force clock update
- **Z0 long press**: Open settings
- **Z1 tap on badge**: Switch sparkline period
- **Z1 double-tap**: Toggle live mode (400ms window)
- **Z1 tap elsewhere**: Force refresh BTC + sparkline
- **Z2 tap**: Force refresh Lemon dollar

## NVS Persistence

Uses ESP32 `Preferences` library (NVS flash partition):

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| WiFi SSID | string | — | Saved network name |
| WiFi pass | string | — | Saved network password |
| Brightness | uint8_t | 128 | Display backlight (0–255) |
| Sound | bool | true | Audio enable/disable |
| Alerts | bool | true | BTC price alert enable/disable |

`nvsFactoryReset()` erases all keys and restarts provisioning flow.

## WiFi Provisioning Flow

1. Device creates AP: `LemonDisplay-XXXX` (last 4 of MAC)
2. QR code displayed on screen encodes `WIFI:S:LemonDisplay-XXXX;T:nopass;;`
3. User scans QR → phone connects to AP
4. Captive portal serves a web page (ESPAsyncWebServer on port 80)
5. Page scans for nearby networks, user selects one and enters password
6. Credentials POSTed to `/save`
7. Device stores in NVS, tears down AP, connects as STA
8. On failure: shows retry/reconfigure screen

## Boot Sequence

```
setup()
  ├── Serial.begin(115200)
  ├── nvsInit()
  ├── displaySetup() + setBrightness(saved)
  ├── dashboardSetup() + drawLoading(LOAD_LOGO)
  ├── audioSetup() + setEnabled(saved)
  ├── touchSetup()
  ├── appInit()
  ├── Register touch callback + redraw callback
  ├── Register scheduler tasks (clock, btc, sparkline, lemon)
  ├── [has WiFi?]
  │   ├── yes → drawLoading(LOAD_WIFI) → tryConnect
  │   │         ├── connected → enterDashboard()
  │   │         └── failed → drawWifiFailedScreen()
  │   └── no → startProvisioning() → drawQR
  └── playStartup()
```

`enterDashboard()` sub-sequence:
```
  LOAD_NTP → timeSetup() + wait up to 5s
  LOAD_DATA → fetchBtcPrice() + init animator
  LOAD_DOLLAR → fetchLemonPrice() + init animators
  LOAD_CHART → fetchSparkline()
  LOAD_DONE → 300ms pause
  → dashboardDrawAll() → SCREEN_DASHBOARD
```

## Display Configuration

- **Panel**: ST7701S (SPI init + 16-bit parallel RGB data)
- **Bus**: 16-bit RGB parallel at 14MHz
- **Resolution**: 480x480
- **Buffering**: PSRAM double buffer (`use_psram = 2`)
- **Backlight**: PWM on GPIO 44 via `Light_PWM`
- **Critical flags**: `de_idle_high = 1` (required for this panel)

See [HARDWARE.md](HARDWARE.md) for the complete GPIO map and `lgfx_matouch_40.h` for the LovyanGFX config.

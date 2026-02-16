# Hardware Reference

## Board

**MaTouch ESP32-S3 4.0" Parallel TFT with Touch** by Makerfabs.

- MCU: ESP32-S3 (dual-core Xtensa LX7, 240MHz)
- Flash: 16MB QIO
- PSRAM: 8MB OPI
- Display: 4.0" 480x480 IPS RGB (ST7701S controller)
- Touch: GT911 capacitive (I2C)
- USB: USB-Serial/JTAG (CDC_ON_BOOT=0 → Serial on UART0)

## GPIO Pin Map

### Display — 16-bit Parallel RGB Bus

| Signal | GPIO | Color |
|--------|------|-------|
| D0 | 6 | B0 |
| D1 | 7 | B1 |
| D2 | 15 | B2 |
| D3 | 16 | B3 |
| D4 | 8 | B4 |
| D5 | 0 | G0 |
| D6 | 9 | G1 |
| D7 | 14 | G2 |
| D8 | 47 | G3 |
| D9 | 48 | G4 |
| D10 | 3 | G5 |
| D11 | 39 | R0 |
| D12 | 40 | R1 |
| D13 | 41 | R2 |
| D14 | 42 | R3 |
| D15 | 2 | R4 |

### Display — Sync & Clock

| Signal | GPIO |
|--------|------|
| HSYNC | 5 |
| VSYNC | 4 |
| PCLK | 21 |
| DE (HENABLE) | 45 |

Bus frequency: 14MHz. Timing:

| Parameter | Value |
|-----------|-------|
| HSYNC front porch | 10 |
| HSYNC pulse width | 8 |
| HSYNC back porch | 50 |
| VSYNC front porch | 10 |
| VSYNC pulse width | 8 |
| VSYNC back porch | 20 |
| HSYNC polarity | 0 (active low) |
| VSYNC polarity | 0 (active low) |
| PCLK idle high | 0 |
| DE idle high | 1 |

### Display — ST7701S SPI Init

The ST7701S requires SPI commands for panel initialization before RGB data can flow.

| Signal | GPIO |
|--------|------|
| CS | 1 |
| SCLK | 12 |
| MOSI | 11 |

### Backlight

| Signal | GPIO |
|--------|------|
| BL (PWM) | 44 |

Controlled via LovyanGFX `Light_PWM`. Brightness 0–255, persisted in NVS.

### Touch — GT911 (I2C1)

| Signal | GPIO |
|--------|------|
| SDA | 17 |
| SCL | 18 |
| RST | 38 |
| INT | NC (not connected) |

I2C frequency: 400kHz. Bus: `I2C_NUM_1`.

### Audio — MAX98357A (I2S)

| Signal | GPIO |
|--------|------|
| DOUT (DIN) | 19 |
| BCLK | 20 |
| LRCK (WS) | 46 |

Used for startup chime and BTC price alert tones.

## PlatformIO Board Config

```ini
[env:matouch_esp32s3_40]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi
board_build.flash_mode = qio
board_build.psram_type = opi
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags =
    -DBOARD_HAS_PSRAM
    -DCONFIG_SPIRAM_USE_MALLOC
    -DARDUINO_USB_CDC_ON_BOOT=0
```

## Flashing

Upload port: USB-Serial/JTAG (typically COM7 on Windows).

To enter download mode manually:
1. Disconnect USB
2. Hold the FLASH button
3. Connect USB
4. Wait 2 seconds
5. Release FLASH

Normal upload via PlatformIO usually handles this automatically.

## PSRAM Usage

Double-buffered display rendering (`use_psram = 2` in LovyanGFX config). Each zone sprite is allocated in PSRAM, rendered off-screen, then pushed to the display framebuffer. This eliminates visible tearing.

Total PSRAM budget: 8MB OPI. Typical usage ~15% (dashboard sprites + HTTP buffers).

# Lemon Box — Agent Instructions

ESP32-S3 firmware for the Lemon Box desk device (MaTouch 4.0", 480x480, 16MB flash, 8MB PSRAM).

## Build Commands

```bash
# V1 production
python -m platformio run -e matouch_esp32s3_40

# V2 real-time (live data, OTA-enabled)
python -m platformio run -e matouch_esp32s3_40_v2_real

# V2 offline demo
python -m platformio run -e matouch_esp32s3_40_v2_demo

# SPIFFS image
python -m platformio run -e matouch_esp32s3_40 -t buildfs

# Upload via USB
python -m platformio run -e matouch_esp32s3_40 -t upload

# Tests
python -m unittest discover -s tools -p "test_*.py"

# Syntax check
python -m py_compile <file>
```

## Flashing Devices

**CRITICAL: Always use `--flash-mode keep --flash-size keep` with esptool.**
Never force `qio` — the ESP32-S3 images use DIO and forcing QIO causes boot loops.

### Flash Modes

- **Normal flash**: writes `firmware.bin` at `0x10000` only (device has working bootloader)
- **Recovery flash**: erase + write all partitions:
  - `0x0` bootloader.bin
  - `0x8000` partitions.bin
  - `0xe000` boot_app0.bin
  - `0x10000` firmware.bin
  - `0xc90000` spiffs.bin

### Steps

1. Detect port: ESP32-S3 has USB VID `0x303A`
2. Enter bootloader if needed: hold BOOT, press RESET, release BOOT
3. Build from clean git worktree (avoid uncommitted changes)
4. Flash with `esptool` using `--flash-mode keep --flash-size keep`
5. Verify boot: stable USB port = OK, port appearing/disappearing = boot loop

### Recovery Flash Commands

```bash
python -m esptool --chip esp32s3 --port COMX --baud 921600 --before default-reset --after no-reset erase-flash

python -m esptool --chip esp32s3 --port COMX --baud 921600 --before no-reset --after no-reset write-flash --flash-mode keep --flash-size keep 0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin 0xc90000 spiffs.bin

python -m esptool --chip esp32s3 --port COMX --baud 921600 --before no-reset --after hard-reset verify-flash 0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin 0xc90000 spiffs.bin
```

## OTA (Remote Flashing Without USB)

The device auto-updates from GitHub Releases (`frxnnk/lemon-display`).

### Publishing an update

1. Build: `python -m platformio run -e matouch_esp32s3_40_v2_real`
2. Binary at: `.pio/build/matouch_esp32s3_40_v2_real/firmware.bin`
3. Compute MD5: `certutil -hashfile firmware.bin MD5`
4. Create GitHub release:
   - Tag must be newer than `APP_VERSION` in `src/config.h` (semver with prerelease)
   - Upload asset as `firmware-v2.bin` (V2) or `firmware.bin` (V1)
   - Release body must contain: `firmware-v2.bin MD5: <32-char-lowercase-hash>`
5. Devices auto-detect within the probe interval and flash themselves

### Remote trigger via HTTP

```bash
curl -X POST http://<device-ip>/api/ota -H "Content-Type: application/json" -d '{}'
```

### Version comparison

Semver with prerelease: `5.1.1-beta.49 < 5.1.1-beta.50 < 5.1.1`. Stable (no suffix) always wins over any prerelease of the same M.m.p.

## Key Files

- `src/config.h` — APP_VERSION, endpoints, intervals, pins
- `src/main.cpp` — V1 entry point
- `src/v2_runtime.cpp` — V2 entry point (OTA, network, scenes)
- `src/ota_manager.cpp` — GitHub Releases check + download + flash
- `src/config_server.cpp` — REST API (includes `/api/ota` endpoint)
- `tools/lemon_flasher.py` — GUI flasher tool
- `tools/flash_v2_real_canary.ps1` — Canary flash script
- `platformio.ini` — Build configuration
- `docs/HARDWARE.md` — Pin mapping and hardware details
- `docs/v2/` — V2 product and architecture documentation

## Code Style

- No comments unless explicitly requested
- Follow existing patterns in neighboring files
- ESP32-S3 target: 16MB Flash, 8MB OPI PSRAM, DIO flash mode
- C++ with Arduino framework, LovyanGFX for display
- Cooperative scheduler (no RTOS), single-core loop
- PSRAM for display sprites, heap-sensitive TLS management

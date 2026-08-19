---
name: lemon-box-flasher
description: Flash Lemon Box ESP32-S3 devices via USB. Use when the user wants to flash, update, recover, or diagnose a Lemon Box device. Also use when the user mentions "flashear cajita", "flash lemon", "update firmware", "bricked device", "boot loop", "recovery flash", or anything related to loading firmware onto the Lemon Display hardware.
---

# Lemon Box Flasher

Flash, update, and recover Lemon Box (MaTouch ESP32-S3 4.0" touchscreen) devices.

## Hardware

| Component | Value |
|-----------|-------|
| Board | MaTouch ESP32-S3 4.0" (Makerfabs) |
| MCU | ESP32-S3, 16MB Flash, 8MB OPI PSRAM |
| Display | 4.0" 480x480 IPS RGB, ST7701S |
| Touch | GT911 capacitive (I2C) |
| Flash mode | DIO (NOT qio) |
| USB | USB-Serial/JTAG |

## Prerequisites

- Python 3.13+ with `esptool` and `pyserial`
- PlatformIO Core (`python -m platformio`)
- USB cable connected to device

## Flashing Workflow

### 1. Detect device

```python
import serial.tools.list_ports as lp
ports = [(p.device, p.vid, p.description) for p in lp.comports()
         if p.vid is not None and "BTHENUM" not in (p.hwid or "").upper()]
```

ESP32-S3 has VID `0x303A`. If no port appears, the device may be in boot loop or disconnected.

### 2. Choose flash mode

**Normal flash** - device has working bootloader, just updating firmware:
- Only writes `firmware.bin` at offset `0x10000`

**Recovery flash** - device is bricked, boot looping, first-time setup, **or had any prior flash interrupted mid-stream**:
- Erases entire flash
- Writes: `bootloader.bin` (0x0), `partitions.bin` (0x8000), `boot_app0.bin` (0xe000), `firmware.bin` (0x10000), `spiffs.bin` (0xc90000)

**Rule:** If a flash got cut off (lost USB connection, esptool error mid-write, etc.), the bootloader/partition table may be in an inconsistent state. A subsequent normal reflash only rewrites `firmware.bin` and will *appear* successful (hash verified, port stable) but the screen stays black because the bootloader can't load the app cleanly. **Always escalate to recovery flash after an interrupted flash, even if a normal reflash seems to succeed.**

### 3. Build clean firmware (if needed)

Always build from a clean checkout to avoid mixing uncommitted changes:

```bash
# Use worktree for isolation
git worktree add /path/to/worktree -b flash-clean-<commit> <commit>
cd /path/to/worktree
python -m platformio run -e matouch_esp32s3_40
```

Build output at `.pio/build/matouch_esp32s3_40/`:
- `bootloader.bin`, `firmware.bin`, `partitions.bin`
- `spiffs.bin` requires `data/cert/x509_crt_bundle.bin` in the source tree

`boot_app0.bin` lives at `~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin`

### 4. Flash with esptool

**CRITICAL: Always use `--flash-mode keep --flash-size keep`.**
Never force `qio` or `16MB` - the images have correct DIO headers and forcing QIO causes boot loops.

**Normal flash:**
```bash
python -m esptool --chip esp32s3 --port COMX --baud 921600 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode keep --flash-size keep 0x10000 firmware.bin
```

**Recovery flash:**
```bash
# Step 1: Erase
python -m esptool --chip esp32s3 --port COMX --baud 921600 \
  --before default-reset --after no-reset erase-flash

# Step 2: Write all partitions
python -m esptool --chip esp32s3 --port COMX --baud 921600 \
  --before no-reset --after no-reset write-flash \
  --flash-mode keep --flash-size keep \
  0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin \
  0x10000 firmware.bin 0xc90000 spiffs.bin

# Step 3: Verify
python -m esptool --chip esp32s3 --port COMX --baud 921600 \
  --before no-reset --after hard-reset verify-flash \
  0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin \
  0x10000 firmware.bin 0xc90000 spiffs.bin
```

### 5. Verify boot

After flash, check for boot loop by monitoring USB port stability:

```python
import time, serial.tools.list_ports as lp
end = time.time() + 12
prev = None
while time.time() < end:
    ports = {p.device for p in lp.comports()}
    if ports != prev:
        print(time.strftime('%H:%M:%S'), ports if ports else 'NO_PORTS')
        prev = ports
    time.sleep(0.25)
```

- **Stable port** = USB-Serial/JTAG up — necessary but **NOT sufficient** to confirm firmware is OK
- **Port appearing/disappearing** = boot loop (needs recovery flash)

**Important:** the USB-Serial/JTAG on the ESP32-S3 lives in hardware, independent of firmware. A stable port only proves the chip is alive — it does NOT prove the bootloader loaded the app or that the screen works. **Always ask the user to visually confirm the Lemon logo / dashboard on the screen.** If the port is stable but the screen is black, the firmware is not running correctly → escalate to recovery flash.

### 6. Entering bootloader manually

If the device doesn't respond or is in boot loop:
1. Hold BOOT button
2. Press and release RESET
3. Release BOOT
4. Device stays in bootloader (USB-Serial/JTAG mode)

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Boot loop (USB connect/disconnect sound) | Flash mode mismatch (QIO forced on DIO hardware) | Recovery flash with `--flash-mode keep` |
| `verify-flash` fails on bootloader | esptool patches header during write | Normal; `write-flash` hash verified OK |
| No serial output after flash | `CDC_ON_BOOT=0`, logs go to UART0 not USB | Expected; check screen instead |
| Port busy / not found | Device rebooted out of bootloader | Re-enter bootloader (BOOT+RESET), then esptool with `--before no-reset` |
| Flash interrupted mid-write (USB drop, esptool error) | Bootloader/partitions left inconsistent | Recovery flash. Normal reflash will pass hash check but screen stays black |
| Hash verified + port stable + screen black | Bootloader/partitions corrupt from prior interrupted flash | Recovery flash |
| `spiffs.bin` missing in clean build | `data/cert/` empty in worktree | Copy from main repo or run `gen_crt_bundle.py` |

## Key Files in Repo

- `tools/lemon_flasher.py` - GUI flasher tool
- `tools/test_lemon_flasher.py` - Unit tests (verifies keep mode)
- `platformio.ini` - Build config (board, flash size, PSRAM)
- `src/config.h` - Firmware version, API endpoints
- `docs/HARDWARE.md` - Pin mapping, flash procedure
- `.pio/build/matouch_esp32s3_40/` - Build output directory

## OTA Updates

OTA (`src/ota_manager.cpp`) uses ESP32 `Update` library to write the app partition directly. It does NOT use esptool or flash-mode flags, so OTA updates are unaffected by the flash-mode issue. OTA only works when bootloader and partitions are already correct (after a proper recovery flash).
## Repo location

This skill lives in the firmware repo so another machine can clone it.

- Path in repo: skills/lemon-box-flasher/
- Install for Codex on another PC: copy skills/lemon-box-flasher to ~/.agents/skills/lemon-box-flasher or CODEX_HOME/skills/lemon-box-flasher
- The packed lemon-box-flasher.skill zip is local-only and gitignored.

## USDT production flash

For the live USDT Control Room, do not use firmware-v2.bin.

- Branch: codex/lemon-usdt-ota
- Env: matouch_esp32s3_40_usdt
- Release asset: firmware-usdt.bin from latest GitHub release
- Verify MD5 from the release body (firmware-usdt.bin MD5) before writing
- Normal flash still writes the app at 0x10000 with --flash-mode keep --flash-size keep
- Recovery uses .pio/build/matouch_esp32s3_40_usdt/ artifacts from that env, not V1/V2
- After boot, Overview must show USDt ARS price, rotating variation, Lemon Yield, PEG. No Intel/Alert.

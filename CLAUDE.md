# Lemon Box - Project Rules

## Build & Test Commands

- Build firmware: `python -m platformio run -e matouch_esp32s3_40`
- Build SPIFFS: `python -m platformio run -e matouch_esp32s3_40 -t buildfs`
- Run flasher tests: `python -m unittest discover -s tools -p "test_*.py"`
- Syntax check: `python -m py_compile <file>`

## Flashing Lemon Box Devices

**CRITICAL: Always use `--flash-mode keep --flash-size keep` with esptool.**
Never force `qio` - the ESP32-S3 images use DIO and forcing QIO causes boot loops.

- Normal flash: writes `firmware.bin` at `0x10000` only
- Recovery flash: erase + write all partitions (bootloader, partitions, boot_app0, firmware, spiffs)
- Always build from a clean git worktree to avoid mixing uncommitted changes
- Use `lemon-box-flasher` skill for the full flashing workflow
- See `docs/HARDWARE.md` for pin mapping and hardware details

## Code Style

- No comments unless explicitly requested
- Follow existing patterns in neighboring files
- ESP32-S3 target: 16MB Flash, 8MB OPI PSRAM, DIO flash mode

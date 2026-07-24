# Lemon Box Studio

Local web dashboard for Lemon Box device operations and UI preset work.

## Run

```powershell
python -m studio --host 127.0.0.1 --port 8765
```

Open `http://127.0.0.1:8765`.

## What v1 Supports

- USB port scan for ESP32-S3 devices.
- Local build and recovery asset readiness.
- Latest release lookup and firmware download.
- PlatformIO build job.
- Normal and recovery flash jobs through the existing flasher service.
- Boot stability check through USB port monitoring.
- Local UI manifest for theme, layout, watchlist, and release notes.

## Flash Safety

- Normal flash writes only `firmware.bin` at `0x10000`.
- Recovery flash requires explicit recovery confirmation.
- Flashing delegates to `tools/lemon_flasher.py`, which keeps `--flash-mode keep` and `--flash-size keep`.
- A stable USB port is not enough to prove success. Confirm the Lemon logo or dashboard appears on the device screen after flashing.

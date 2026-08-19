# USDT Control Room — flash y OTA

Firmware productivo para cajitas Lemon Box (MaTouch ESP32-S3 4.0").
Canal aislado de V1 (`firmware.bin`) y V2 (`firmware-v2.bin`).

## Que muestra

Pantalla 1 / Overview:

1. Precio del Dolar Digital (USDt) en ARS — CriptoYa `lemoncash/usdt/ars`
2. Variacion 1h / 24h / 7d rotando — CoinGecko Tether
3. Rendimiento Lemon Yield — `https://api.lemoncash.com.ar/api/v1/interest-funds-percentages` fila `currency=USDt` + `protocol=LEMON_YIELD` (live, no hardcode 2.5%)
4. PEG vs USD — CoinGecko Tether USD

Otras pantallas: Networks (BEP20, Polygon/MATIC, TRC20, ERC20 +7), Markets, Regions, System.
Intel y Alert no existen en este firmware.

## Build

```bash
python -m platformio run -e matouch_esp32s3_40_usdt
```

Binario: `.pio/build/matouch_esp32s3_40_usdt/firmware.bin`
Asset OTA: `firmware-usdt.bin`
Version: `5.1.1-usdt.1` (`APP_VERSION` cuando `LEMON_USDT_MODE=1`)

## Flash USB inicial (DIO keep)

Nunca forzar `qio`. Solo ` --flash-mode keep --flash-size keep`.

Normal (bootloader sano):

```bash
python -m esptool --chip esp32s3 --port COMX --baud 921600 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode keep --flash-size keep \
  0x10000 firmware-usdt.bin
```

Recovery (primera vez, boot loop, o flash interrumpido):

```bash
python -m esptool --chip esp32s3 --port COMX --baud 921600 \
  --before default-reset --after no-reset erase-flash

python -m esptool --chip esp32s3 --port COMX --baud 921600 \
  --before no-reset --after no-reset write-flash \
  --flash-mode keep --flash-size keep \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin \
  0xc90000 spiffs.bin
```

Los artefactos de recovery salen de `.pio/build/matouch_esp32s3_40_usdt/` y `boot_app0.bin` del package Arduino ESP32.

## OTA automatica

Despues del primer USB, la cajita:

1. Consulta `https://api.github.com/repos/frxnnk/lemon-display/releases/latest`
2. Compara el tag contra `APP_VERSION` (`5.1.1-usdt.1`)
3. Busca exactamente el asset `firmware-usdt.bin`
4. Exige MD5 en el body: `firmware-usdt.bin MD5: <32 hex lowercase>`
5. Descarga, flashea y reinicia sola

Publicar un update:

1. Subir `APP_VERSION`
2. Build `matouch_esp32s3_40_usdt`
3. Release con tag semver mas nuevo, asset `firmware-usdt.bin`, MD5 en el body

No mezclar `firmware-v2.bin` en este canal. Las cajas V2 ignoran este asset.

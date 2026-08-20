# USDT Control Room — flash y OTA

Firmware productivo para cajitas Lemon Box (MaTouch ESP32-S3 4.0").
Canal aislado de V1 (`firmware.bin`) y V2 (`firmware-v2.bin`).

## Que muestra

Pantalla 1 / Overview:

1. Precio de Tether USDt en ARS — CriptoYa `lemoncash/usdt/ars`
2. Variacion ARS 1h / 24h / 7d rotando — calculada desde el chart horario de CoinGecko
3. Rendimiento Lemon Yield — `https://api.lemoncash.com.ar/api/v1/interest-funds-percentages` fila `currency=USDt` + `protocol=LEMON_YIELD` (live, no hardcode 2.5%)
4. PEG vs USD — Coinbase `exchange-rates?currency=USDT`

Otras pantallas: Networks (supply on-chain y cambio 24h para BEP20,
Polygon/MATIC, TRC20 y ERC20, mas el listado de las otras 7), Markets,
Regions (Argentina, Brasil, Peru y Colombia) y System.
Intel y Alert no existen en este firmware.

## Fuentes y degradacion

| Dato | Fuente | Cadencia | Comportamiento ante fallo |
|------|--------|----------|----------------------------|
| USDt/ARS Lemon, bid y ask | CriptoYa | 60 s | conserva el ultimo valor y marca su freshness |
| PEG USD + BRL/PEN/COP | Coinbase | 60 s | PEG y regiones se validan por separado |
| Variaciones 1h/24h/7d ARS | CoinGecko market chart | 30 min | reintenta en 60 s si nunca obtuvo un chart valido |
| Rendimiento USDt | Lemon API `LEMON_YIELD` | 60 s | conserva el ultimo APR valido |
| Supply por red + cambio 24h | DefiLlama stablecoins | 15 min | conserva la ultima distribucion valida; reintenta en 60 s |

El header prioriza la salud del precio Lemon y usa `REINTENTO` en vez de un error
generico. System muestra el estado individual de precio, PEG/regiones, variaciones,
yield y redes. La caja espera a que NTP entregue una hora valida antes de abrir TLS.
Los requests de datos y los chequeos OTA corren en un worker FreeRTOS separado, por
lo que touch y navegacion siguen funcionando durante timeouts o respuestas lentas.

Si `COINGECKO_API_KEY` esta vacia o conserva `YOUR_COINGECKO_DEMO_KEY`, el cliente
no la envia. CoinGecko permite entonces la consulta publica; enviar el placeholder
provoca `401` y deja las variaciones sin datos.

## Build

```bash
python -m platformio run -e matouch_esp32s3_40_usdt
```

Binario: `.pio/build/matouch_esp32s3_40_usdt/firmware.bin`
Asset OTA: `firmware-usdt.bin`
Version: `5.1.1-usdt.7` (`APP_VERSION` cuando `LEMON_USDT_MODE=1`)

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
2. Compara el tag contra `APP_VERSION` (`5.1.1-usdt.7`)
3. Busca exactamente el asset `firmware-usdt.bin`
4. Exige MD5 en el body: `firmware-usdt.bin MD5: <32 hex lowercase>`
5. Descarga, flashea y reinicia sola

Publicar un update:

1. Subir `APP_VERSION`
2. Build `matouch_esp32s3_40_usdt`
3. Release con tag semver mas nuevo, asset `firmware-usdt.bin`, MD5 en el body

No mezclar `firmware-v2.bin` en este canal. Las cajas V2 ignoran este asset.

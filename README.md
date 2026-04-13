# Lemon Box

Dashboard crypto de escritorio corriendo sobre MaTouch ESP32-S3 con pantalla redonda táctil de 480×480. Bitcoin en tiempo real vía WebSocket, 5 pares, Polymarket, dólar argentino, OTA updates y USB flasher — todo open-hardware, sin abrir el browser.

> **v4.9.7** · ESP32-S3 · 480×480 Round · PSRAM 8MB

<!-- TODO: foto/render del dispositivo armado -->

## Features

### Data
- **BTC Real-time** — Conexión WebSocket directa a Binance: cada tick llega en menos de un segundo. Líneas y velas OHLC en 8 timeframes (1m → 1D).
- **Multi-par** — BTC/USD, BTC/ETH, BTC/SOL, BTC/ARS y BTC/ORO. Cambio entre pares con un toque.
- **Polymarket** — Probabilidades en vivo sobre elecciones, cripto y deportes, con countdown al cierre del mercado.
- **Dólar** — USDT/ARS (Lemon Cash) vía CriptoYa, con sparkline de 24h a 1 año.

### Sistema
- **OTA Updates** — Firmware over-the-air desde GitHub Releases. Siempre en la última versión, sin cables.
- **USB Flasher** — Herramienta standalone para flashear/recuperar el device desde Windows (ver `tools/`).
- **WiFi QR Provisioning** — Scanea QR, conectate al AP, ingresá tus credenciales vía captive portal.
- **Audio Alerts** — Speaker I2S MAX98357A: beep cuando BTC se mueve más de 5% en una hora o cuando un countdown de Polymarket está por terminar.
- **NVS Persistence** — Brillo, sonido, alertas, credenciales WiFi y preferencias sobreviven al reboot.
- **Touch Gestures** — Tap para refresh, long-press en el header para settings, double-tap para live mode (60s → 10s polling).
- **Zone Rendering** — Sprite-based por zona con PSRAM double buffering, sin tearing.
- **Factory Reset** — Reset completo desde settings o secuencia hardware.

## Hardware

| Componente | Spec |
|---|---|
| Board | MaTouch ESP32-S3 4.0" (Makerfabs) |
| Display | 4.0" 480×480 IPS RGB, ST7701S controller |
| Touch | GT911 capacitivo (I2C) |
| Audio | MAX98357A I2S amplifier |
| MCU | ESP32-S3, 16MB Flash, 8MB OPI PSRAM |
| Carcasa | PLA impresa en 3D (STLs en `landing/modelo3d/`) |

Pin mapping completo en [`docs/HARDWARE.md`](docs/HARDWARE.md).

## Quick Start

### 1. Clonar

```bash
git clone https://github.com/frxnnk/lemon-display.git
cd lemon-display
```

### 2. Configurar secrets

```bash
cp src/secrets.h.example src/secrets.h
```

Editar `src/secrets.h` con una [CoinGecko Demo API key](https://www.coingecko.com/en/api/pricing) (tier gratuito: 30 calls/min).

### 3. Build & flash

```bash
pio run -e matouch_esp32s3_40
pio run -e matouch_esp32s3_40 -t upload
pio device monitor -b 115200
```

Al primer boot, el device muestra un QR para provisioning WiFi. Scan con el celular, conectate al AP `Lemon Box Setup`, e ingresá las credenciales de tu red en el captive portal.

### Flasheo alternativo (usuarios finales)

Para usuarios sin toolchain de embedded, usar el flasher standalone:

```bash
cd tools
python LemonFlasher.py    # o el .exe empaquetado
```

> **Importante**: al flashear, usar siempre `--flash-mode keep --flash-size keep`. Nunca forzar `qio`. Los binarios del ESP32-S3 están en DIO y forzar QIO produce boot loops.

## Project Structure

```
lemon-box/
├── src/                      # Firmware ESP32-S3 (C++)
│   ├── main.cpp              # Entry, state machine, scheduler
│   ├── config.h              # Versión, endpoints, pins
│   ├── api_client.*          # Binance + CoinGecko + CriptoYa + Polymarket
│   ├── ota_manager.*         # OTA desde GitHub Releases
│   ├── ui_*.cpp              # Dashboard, settings, tutorial
│   ├── audio_manager.*       # I2S alerts
│   ├── wifi_provision.*      # QR + captive portal
│   └── data/                 # Fonts + brand bitmaps embebidos
├── tools/                    # USB flasher + asset converters
│   ├── LemonFlasher.py       # GUI flasher standalone
│   └── png_to_rgb565.py      # PNG → RGB565 C header
├── landing/                  # Landing web (Three.js, deployada en Vercel)
│   ├── index.html            # Single-file landing con scroll-driven 3D
│   └── modelo3d/             # STLs del case
├── docs/
│   ├── SYSTEM_DESIGN.md      # Arquitectura del firmware
│   ├── HARDWARE.md           # Pin mapping
│   └── lemon_brand_guidelines.md
└── platformio.ini
```

## Landing page

La landing (single-page, Three.js, scroll-driven 3D assembly) vive en [`landing/`](landing/) y se deploya a Vercel. Es autocontenida — `cd landing && vercel --prod` y queda live.

## APIs

- **Binance** — WebSocket de ticker/kline para BTC (real-time)
- **CoinGecko** — Market chart y dominance
- **CriptoYa** — USDT/ARS de Lemon Cash
- **Polymarket** — Probabilidades de eventos

## Arquitectura

Detalle completo en [`docs/SYSTEM_DESIGN.md`](docs/SYSTEM_DESIGN.md): data flow, scheduler, animation system, zone rendering con PSRAM, WiFi provisioning y OTA.

## Autor

Hecho por **Ferced** para la comunidad [Lemon](https://lemon.me).

## License

Proprietary. Brand assets y guidelines son propiedad de [Lemon](https://lemon.me).
El modelo de licencia para el código/hardware del proyecto aún no está definido.

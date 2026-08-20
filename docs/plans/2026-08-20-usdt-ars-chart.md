# USDT/ARS 7D Chart Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Mostrar en Markets un grafico ancho USDT/ARS de siete dias con minimo, maximo y precio actual.

**Architecture:** Reutilizar `COINGECKO_USDT_CHART_EP`, reducir la serie dentro del parser y guardarla en `UsdtPegData`. Dibujar la serie mediante primitivas LovyanGFX sin dependencias nuevas.

**Tech Stack:** C++ Arduino, ArduinoJson, LovyanGFX, Python unittest, PlatformIO.

---

### Task 1: Persistir la serie reducida

**Files:**
- Modify: `src/usdt_lemon_data.h`
- Modify: `src/usdt_lemon_data.cpp`
- Test: `tools/test_usdt_firmware.py`

1. Escribir un test que exija una serie ARS acotada en `UsdtPegData` y downsampling en `parseMarketChart`.
2. Ejecutar el test y confirmar que falla por ausencia de la serie.
3. Agregar un buffer fijo de 48 puntos y poblarlo uniformemente desde `prices`.
4. Ejecutar el test focal y la suite completa.

### Task 2: Dibujar el grafico en Markets

**Files:**
- Modify: `src/usdt_lemon_ui.cpp`
- Test: `tools/test_usdt_firmware.py`

1. Escribir un test que exija `drawArsChart`, una card inferior de ancho completo y labels MIN/MAX/ACTUAL.
2. Ejecutar el test y confirmar que falla por ausencia del grafico.
3. Implementar el escalado seguro, la linea y los labels; mostrar loading cuando no haya serie.
4. Eliminar las cards inferiores `24H ARS` y `SPREAD` de Markets.
5. Ejecutar tests focales y suite completa.

### Task 3: Empaquetar y publicar OTA

**Files:**
- Modify: `src/config.h`
- Modify: `docs/USDT_OTA.md`
- Modify: `firmware-usdt.bin`
- Test: `tools/test_usdt_firmware.py`

1. Escribir el test de version `.18` y confirmar que falla.
2. Subir `APP_VERSION`, actualizar la guia y ejecutar la suite.
3. Compilar limpio `matouch_esp32s3_40_usdt`.
4. Copiar el binario, verificar version y hashes, y commitear solo los archivos del alcance.
5. Subir la rama OTA, publicar `.18` como Latest y verificar el asset descargado.


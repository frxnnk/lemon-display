# Lemon Box V2 Real Canary Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use `executing-plans` to implement this plan task-by-task.

**Goal:** Ejecutar en una sola Lemon Box existente una UI V2 persistente con hora, BTC y watchlist reales, Market Tape y Settings táctiles, manteniendo separados el firmware normal, la demo determinística y el binario de rollback.

**Architecture:** Agregar un entorno `matouch_esp32s3_40_v2_real` que reutiliza los módulos reales de hardware, NVS, Wi-Fi, NTP, CoinGecko, Yahoo, Studio y touch, pero entra a un runtime V2 acotado antes del dashboard legado. La navegación y freshness se modelan como funciones puras/`constexpr`; el render consume snapshots y nunca obtiene datos por su cuenta. La demo de 78 segundos permanece detrás de `LEMON_V2_DEMO_MODE`.

**Tech Stack:** Arduino/ESP32-S3, PlatformIO, LovyanGFX, ESP32 Preferences, HTTPClient/WiFiClientSecure, Python `unittest`, esptool.

**Safety boundary:** Sin push, release OTA, erase-flash ni actualización de otra unidad. El flash normal sólo puede escribir `firmware.bin` en `0x10000` usando `--flash-mode keep --flash-size keep`, después de guardar un rollback de la app actual e identificar la unidad por VID/PID y número de serie.

---

### Task 1: Contrato navegable y freshness

**Files:**
- Create: `tools/test_v2_runtime.py`
- Create: `src/v2_runtime_model.h`

1. Escribir primero tests de contrato que exijan escenas `HOME`, `MARKET_TAPE`, `CONTEXT`, `SETTINGS`; entrada segura a Settings por long press; back por botón/swipe; timeouts de 30/45/90 segundos; y estados `LOADING`, `LIVE`, `CACHED`, `STALE`, `OFFLINE`, `ERROR`, `RATE_LIMITED`.
2. Ejecutar `python -m unittest tools.test_v2_runtime -v` y confirmar que falla porque el modelo no existe.
3. Implementar funciones puras y `static_assert` con casos representativos.
4. Repetir el test y confirmar verde.

### Task 2: Acceso thread-safe al Market Tape

**Files:**
- Modify: `src/ui_stocks.h`
- Modify: `src/ui_stocks.cpp`
- Test: `tools/test_v2_runtime.py`

1. Agregar un test que exija `stocksGetSnapshotAt(index, out)` sin exponer buffers internos.
2. Confirmar rojo.
3. Implementar el accessor bajo el lock existente, copiando símbolo, quote, spark, status y fetching.
4. Confirmar verde y ejecutar toda la suite.

### Task 3: Preferencias V2 mínimas

**Files:**
- Modify: `src/nvs_storage.h`
- Modify: `src/nvs_storage.cpp`
- Test: `tools/test_v2_runtime.py`

1. Exigir getters/setters para rotación de watchlist con valores permitidos `0`, `15`, `30`, `60` segundos.
2. Confirmar rojo.
3. Persistir bajo una clave V2 corta sin cambiar ni borrar claves existentes.
4. Confirmar verde.

### Task 4: Runtime de datos reales separado del render

**Files:**
- Create: `src/v2_runtime.h`
- Create: `src/v2_runtime.cpp`
- Modify: `src/main.cpp`
- Modify: `src/config.h`
- Modify: `platformio.ini`
- Test: `tools/test_v2_runtime.py`

1. Exigir un entorno `matouch_esp32s3_40_v2_real` con `LEMON_V2_REAL_MODE=1`, dejando demo y normal sin cambios de modo.
2. Confirmar rojo.
3. Inicializar NVS/display/audio/touch; reconectar Wi-Fi guardado o reutilizar provisioning; sincronizar NTP; obtener BTC con `fetchBtcPrice`; activar el worker Yahoo y su burst de watchlist; arrancar Config Server para edición local de watchlist.
4. Mantener `ApiResult` y timestamps para que render distinga loading, cached, stale, offline, error y rate limit.
5. Confirmar verde y compilar el entorno real.

### Task 5: Home, Market Tape, contexto y Settings

**Files:**
- Create: `src/ui_v2_runtime.h`
- Create: `src/ui_v2_runtime.cpp`
- Create: `src/ui_v2_settings.cpp`
- Test: `tools/test_v2_runtime.py`

1. Exigir safe inset de 32 px, fuentes/logos oficiales existentes, fuente de datos visible y ausencia de fixtures/cotizaciones hardcodeadas.
2. Confirmar rojo.
3. Renderizar Home ambiental con hora, BTC real, cambio y stock enfocado; Market Tape con hasta cinco snapshots reales; contexto de una fila; Settings en tres páginas superficiales (Pantalla, Datos, Dispositivo).
4. Hacer utilizables brillo, 12/24h, sonido, rotación de watchlist y cambio de foco. Mostrar Wi-Fi, watchlist/Studio, versión, uptime, heap y estrategia de actualización canary; no habilitar OTA pública desde V2.
5. Mostrar noticias como `Provider pendiente` y no fabricar titulares, Packs ni precios.
6. Confirmar verde y compilar.

### Task 6: Documentación y flash seguro

**Files:**
- Create: `docs/v2/2026-07-22-real-runtime-architecture.md`
- Create: `tools/flash_v2_real_canary.ps1`
- Modify: `docs/v2/2026-07-22-physical-canary-checklist.md`
- Test: `tools/test_v2_runtime.py`

1. Exigir que el script reciba puerto, serial esperado y confirmación; valide VID `0x303A`; no contenga erase; guarde la app actual; escriba sólo `0x10000`; use keep/keep; y emita hashes.
2. Confirmar rojo.
3. Implementar el script y documentar arquitectura, navegación, TTL, proveedores y gaps honestos de noticias/Packs.
4. Ejecutar suite completa, build normal, demo y V2 real; guardar SHA-256 de binarios y revisar la tabla de particiones.
5. Re-detectar el puerto. Ejecutar lectura/backup y flash sólo si serial, particiones y rollback coinciden.
6. Verificar estabilidad del puerto durante al menos 12 segundos y pedir confirmación visual/táctil de Home, Tape, contexto y Settings antes de cerrar.

### Verification commands

```powershell
python -m unittest discover -s tools -p "test_*.py"
python -m platformio run -e matouch_esp32s3_40
python -m platformio run -e matouch_esp32s3_40_v2_demo
python -m platformio run -e matouch_esp32s3_40_v2_real
Get-FileHash -Algorithm SHA256 .pio\build\matouch_esp32s3_40\firmware.bin
Get-FileHash -Algorithm SHA256 .pio\build\matouch_esp32s3_40_v2_demo\firmware.bin
Get-FileHash -Algorithm SHA256 .pio\build\matouch_esp32s3_40_v2_real\firmware.bin
```


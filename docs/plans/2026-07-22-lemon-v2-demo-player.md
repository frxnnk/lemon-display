# Lemon Box V2 Demo Player Implementation Plan

> **For Codex:** Implementar en este mismo worktree preservando cambios previos. No hacer commit, push, OTA ni flash masivo.

**Goal:** Ejecutar el storyboard V2 de 78 segundos sobre una Lemon Box existente con fixtures locales, interacción táctil y cero dependencia de red.

**Architecture:** Agregar un entorno de PlatformIO exclusivo para demo que activa por macro un reproductor de pantalla completa. La build normal permanece idéntica. El módulo nuevo posee timeline, render y touch; `main.cpp` sólo desvía `setup()` y `loop()` cuando la macro está habilitada.

**Tech Stack:** ESP32-S3, Arduino, LovyanGFX, QRCode existente, PlatformIO, unittest Python y scripts de conversión de assets ya incluidos.

---

### Task 1: Contrato del modo demo

**Files:**
- Create: `tools/test_v2_demo_player.py`
- Modify: `platformio.ini`
- Modify: `src/config.h`

**Steps:**

1. Escribir tests que exijan un entorno `matouch_esp32s3_40_v2_demo`, macro por defecto apagada y macro activa sólo en esa build.
2. Ejecutar el test y comprobar que falla porque el entorno aún no existe.
3. Agregar la configuración mínima.
4. Ejecutar el test y comprobar que pasa.

### Task 2: Timeline determinístico

**Files:**
- Create: `src/v2_demo_timeline.h`
- Update: `tools/test_v2_demo_player.py`

**Steps:**

1. Agregar tests para nueve escenas, duración total de 78.000 ms y límites contiguos.
2. Ejecutar y observar el fallo por archivo ausente.
3. Implementar un array `constexpr` con `HOME`, `PREOPEN`, `OPEN`, `TAPE`, `PACK`, `NEWS`, `CONTEXT`, `QR` y `RETURN`.
4. Verificar que el contrato pasa.

### Task 3: Assets autorizados para ESP32

**Files:**
- Create: `src/data/PPNeueMachinaBold24.h`
- Create: `src/data/lemon_v2_logo_light_120.h`
- Create: `src/data/lemon_v2_logo_black_120.h`
- Update: `tools/test_v2_demo_player.py`

**Steps:**

1. Testear que la fuente declara rango ASCII y que cada logo tiene exactamente `120 × 28` píxeles RGB565.
2. Generar PP Neue Machina desde el TTF oficial mediante `tools/ttf_to_gfx.py`.
3. Recortar los logos ya renderizados desde los mockups validados y convertirlos con `tools/png_to_rgb565.py`.
4. Registrar que son derivados del asset oficial y verificar tamaños.

### Task 4: Render de escenas

**Files:**
- Create: `src/ui_v2_demo.h`
- Create: `src/ui_v2_demo.cpp`
- Update: `tools/test_v2_demo_player.py`

**Steps:**

1. Testear los fixtures, tokens RGB565, área segura de 48 px y ausencia de red/recomendaciones.
2. Implementar una única sprite 480 × 480 en PSRAM.
3. Dibujar home, preapertura, apertura, Tape, Pack IA, noticia, contexto, QR y retorno.
4. Actualizar sólo cuando cambia escena, segundo de countdown o fila visible.
5. Implementar toque: `NEWS → CONTEXT → QR → HOME`.

### Task 5: Integración reversible

**Files:**
- Modify: `src/main.cpp`
- Update: `tools/test_v2_demo_player.py`

**Steps:**

1. Testear que `setup()` y `loop()` desvían antes de WiFi cuando la macro está activa.
2. Agregar includes y ramas condicionales mínimas.
3. Compilar la build normal y confirmar que sigue funcionando.
4. Compilar la build de demo y capturar tamaño/hash del firmware.

### Task 6: Canary físico

**Files:**
- Create: `docs/v2/2026-07-22-physical-canary-checklist.md`

**Steps:**

1. Detectar USB VID `0x303A`; no confundir puertos Bluetooth.
2. Si no hay unidad conectada, detenerse antes de cualquier escritura y pedir conexión física.
3. Con unidad autorizada, flashear sólo `firmware.bin` con `--flash-mode keep --flash-size keep`.
4. Verificar estabilidad del puerto y confirmación visual de logo/home.
5. Probar 78 segundos, touch, QR, brillo, legibilidad y temperatura.
6. Si el flash se interrumpe, no repetir normal: pasar a recovery completo siguiendo `lemon-box-flasher`.

## Definition of done

- Build normal verde.
- Build demo verde y offline.
- Suite Python verde.
- Firmware demo identificado por SHA-256.
- Ningún deploy, OTA o cambio masivo.
- Prueba física hecha sólo si aparece una unidad USB y el usuario autoriza ese canary.

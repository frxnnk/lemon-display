# Pantalla de configuración — plan de implementación

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Agregar al firmware una pantalla de diagnóstico que se abre con long
press, muestra versión/commit/build más el estado de red y del feed, y ofrece
dos acciones: refrescar el feed y reaparear WiFi.

**Architecture:** Fase nueva `PHASE_CONFIG` en `ferced_main.cpp` más un módulo
`src/ui_config.{h,cpp}` que dibuja directo a `tft`, sin sprite ni bandas. La
función de dibujo recibe una struct y no consulta nada, para que compile en el
simulador. La geometría de los botones vive en un solo lugar y la usan tanto el
dibujo como la detección de toques.

**Tech Stack:** C++/Arduino sobre ESP32-S3, LovyanGFX, PlatformIO. Script de
inyección en Python. Simulador SDL para verificar sin flashear.

Diseño validado: `docs/plans/2026-08-03-pantalla-configuracion-design.md`

---

## Nota sobre verificación

Este repo **no tiene framework de tests para el firmware**. El bucle real,
documentado en el handoff, es el simulador: se compila, se corre, se mira la
captura. Ese es el paso de verificación de las tareas de UI, y no es opcional —
dos bugs serios del proyecto se encontraron así de un vistazo.

El único componente con lógica pura testeable es el script de inyección de
versión, que sí lleva test.

---

### Task 1: Inyectar versión, commit y fecha en tiempo de compilación

**Files:**
- Create: `tools/inject_version.py`
- Create: `tools/test_inject_version.py`
- Modify: `platformio.ini` (env `ferced_display` y `ferced_display_vps`)

**Step 1: Escribir el test que falla**

```python
# tools/test_inject_version.py
import subprocess
from unittest import mock
import inject_version as iv


def test_commit_limpio_sin_sufijo():
    with mock.patch.object(iv, "_git", side_effect=["abc1234", ""]):
        assert iv.commit_id() == "abc1234"


def test_commit_sucio_lleva_sufijo():
    with mock.patch.object(iv, "_git", side_effect=["abc1234", " M src/x.cpp"]):
        assert iv.commit_id() == "abc1234-dirty"


def test_sin_git_no_rompe_el_build():
    with mock.patch.object(iv, "_git", side_effect=subprocess.CalledProcessError(1, "git")):
        assert iv.commit_id() == "desconocido"
```

**Step 2: Correr el test y ver que falla**

```bash
cd tools && python -m pytest test_inject_version.py -v
```
Esperado: FAIL con `ModuleNotFoundError: No module named 'inject_version'`

**Step 3: Escribir el script**

```python
# tools/inject_version.py
"""Inyecta version, commit y fecha de compilacion.

El commit lleva sufijo -dirty si el arbol tiene cambios sin commitear: sin eso
no hay forma de saber que lo flasheado no corresponde a ningun commit, que es
el caso mas enganoso.
"""
import subprocess
from datetime import datetime

VERSION = "1.0.0"


def _git(*args):
    return subprocess.check_output(("git",) + args, text=True).strip()


def commit_id():
    try:
        short = _git("rev-parse", "--short", "HEAD")
        sucio = _git("status", "--porcelain")
    except Exception:
        return "desconocido"
    return short + "-dirty" if sucio else short


def build_date():
    return datetime.now().strftime("%Y-%m-%d %H:%M")


if __name__ != "__main__":
    try:
        Import("env")  # noqa: F821  (lo provee PlatformIO)
    except NameError:
        pass
    else:
        env.Append(CPPDEFINES=[  # noqa: F821
            ("FERCED_VERSION", env.StringifyMacro(VERSION)),        # noqa: F821
            ("FERCED_COMMIT", env.StringifyMacro(commit_id())),     # noqa: F821
            ("FERCED_BUILD_DATE", env.StringifyMacro(build_date())),# noqa: F821
        ])
        print("[version] %s %s (%s)" % (VERSION, commit_id(), build_date()))
```

**Step 4: Correr el test y ver que pasa**

```bash
cd tools && python -m pytest test_inject_version.py -v
```
Esperado: 3 passed

**Step 5: Cablearlo en platformio.ini**

En `[env:ferced_display]` agregar:
```ini
extra_scripts = pre:tools/inject_version.py
```

En `[env:ferced_display_vps]`, que ya tiene uno, pasar a lista:
```ini
extra_scripts =
	pre:tools/inject_secrets.py
	pre:tools/inject_version.py
```

**Step 6: Verificar que las macros llegan al binario**

```bash
python -m platformio run -e ferced_display_vps
```
Esperado: en la salida aparece `[version] 1.0.0 <hash> (<fecha>)`

**Step 7: Commit**

```bash
git add tools/inject_version.py tools/test_inject_version.py platformio.ini
git commit -m "version: inyectar version, commit y fecha al compilar"
```

---

### Task 2: El módulo de dibujo

**Files:**
- Create: `src/ui_config.h`
- Create: `src/ui_config.cpp`

**Step 1: La cabecera**

```cpp
// src/ui_config.h
#pragma once

#include <cstdint>

// Los datos llegan armados a proposito: si esta pantalla consultara WiFi o NVS
// adentro no compilaria en el simulador, que solo tiene shims de graficos.
struct ConfigInfo {
    const char* version;
    const char* commit;
    const char* built;
    const char* ssid;
    const char* ip;
    const char* endpoint;   // solo el host
    uint32_t    uptimeS;
    float       fps;
    uint8_t     items;
    bool        online;
};

enum ConfigAction : uint8_t {
    CFG_NONE,
    CFG_REFRESH,
    CFG_CLOSE,
    CFG_FORGET,
};

void uiConfigDraw(const ConfigInfo& info);

// Resuelve que boton cae bajo un toque. La geometria vive en el .cpp y la usan
// tanto el dibujo como esto, para que no puedan desincronizarse.
ConfigAction uiConfigHit(int16_t x, int16_t y);
```

**Step 2: La implementación**

```cpp
// src/ui_config.cpp
#include "ui_config.h"

#include "display_manager.h"
#include "design_system.h"
#include "ui_ferced.h"          // FercedColors
#include "data/ferced_mark_11.h"
#include "config.h"

#include <cstdio>

using namespace FercedColors;

namespace {

struct Rect { int16_t x, y, w, h; };

constexpr int MARGEN    = 32;
constexpr int ANCHO     = SCREEN_W - 2 * MARGEN;   // 416
constexpr int VALOR_X   = 200;
constexpr int FILA_H    = 26;

constexpr Rect BTN_REFRESH = { MARGEN, 350, 250, 50 };
constexpr Rect BTN_CLOSE   = { 300,    350, 148, 50 };
constexpr Rect BTN_FORGET  = { MARGEN, 410, ANCHO, 50 };

bool dentro(const Rect& r, int16_t x, int16_t y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void marca(int x, int y, int escala) {
    for (int py = 0; py < 28; py++) {
        for (int px = 0; px < 11; px++) {
            const uint16_t c = pgm_read_word(&ferced_mark_11[py * 11 + px]);
            if (c == 0x0000) continue;
            tft.fillRect(x + px * escala, y + py * escala, escala, escala, c);
        }
    }
}

void fila(int y, const char* etiqueta, const char* valor) {
    tft.setTextDatum(lgfx::top_left);
    tft.setTextColor(FG_3, CANVAS);
    tft.drawString(etiqueta, MARGEN, y, &Satoshi12);
    tft.setTextColor(FG, CANVAS);
    tft.drawString(valor, VALOR_X, y, &Satoshi12);
}

void separador(int y) {
    tft.drawFastHLine(MARGEN, y, ANCHO, LINE);
}

void boton(const Rect& r, const char* texto, uint16_t color) {
    tft.drawSmoothRoundRect(r.x, r.y, 12, 11, r.w, r.h, LINE, CANVAS);
    tft.setTextDatum(lgfx::middle_center);
    tft.setTextColor(color, CANVAS);
    tft.drawString(texto, r.x + r.w / 2, r.y + r.h / 2, &Satoshi12);
}

// "2 h 14 min", o "14 min" cuando no llega a la hora.
void formatearUptime(char* out, size_t n, uint32_t s) {
    const uint32_t h = s / 3600, m = (s % 3600) / 60;
    if (h > 0) snprintf(out, n, "%lu h %lu min", (unsigned long)h, (unsigned long)m);
    else       snprintf(out, n, "%lu min", (unsigned long)m);
}

}  // namespace

void uiConfigDraw(const ConfigInfo& info) {
    displayWaitVSync();
    tft.fillScreen(CANVAS);

    // Encabezado: mark a escala 1 con el wordmark al lado.
    marca(MARGEN, 20, 1);
    tft.setTextDatum(lgfx::middle_left);
    tft.setTextColor(FG, CANVAS);
    tft.drawString("FERCED", MARGEN + 11 + 12, 20 + 14, &SatoshiMedium18);

    int y = 80;
    fila(y, "Version",   info.version);      y += FILA_H;
    fila(y, "Commit",    info.commit);       y += FILA_H;
    fila(y, "Compilado", info.built);        y += FILA_H;

    separador(y + 4); y += 24;

    fila(y, "Red",  info.online ? info.ssid : "sin conexion"); y += FILA_H;
    fila(y, "IP",   info.online ? info.ip : "-");              y += FILA_H;
    fila(y, "Feed", info.endpoint);                            y += FILA_H;

    separador(y + 4); y += 24;

    char buf[32];
    formatearUptime(buf, sizeof(buf), info.uptimeS);
    fila(y, "Encendido", buf); y += FILA_H;
    snprintf(buf, sizeof(buf), "%.1f fps", info.fps);
    fila(y, "Animacion", buf); y += FILA_H;
    snprintf(buf, sizeof(buf), "%u", (unsigned)info.items);
    fila(y, "Items", buf);

    boton(BTN_REFRESH, "Actualizar feed", FG);
    boton(BTN_CLOSE,   "Cerrar",          FG);
    boton(BTN_FORGET,  "Reaparear WiFi",  DANGER);
}

ConfigAction uiConfigHit(int16_t x, int16_t y) {
    if (dentro(BTN_REFRESH, x, y)) return CFG_REFRESH;
    if (dentro(BTN_CLOSE,   x, y)) return CFG_CLOSE;
    if (dentro(BTN_FORGET,  x, y)) return CFG_FORGET;
    return CFG_NONE;
}
```

**Step 3: Verificar que compila**

```bash
python -m platformio run -e ferced_display_vps
```
Esperado: SUCCESS. Todavía no se usa desde ningún lado, sólo tiene que compilar.

**Step 4: Commit**

```bash
git add src/ui_config.h src/ui_config.cpp
git commit -m "config: modulo de dibujo de la pantalla de configuracion"
```

---

### Task 3: Verlo en el simulador — ACÁ SE ITERA EL DISEÑO

**Files:**
- Modify: `sim/build.ps1` (lista de fuentes)
- Modify: `sim/src/sim_main.cpp`

**Step 1: Sumar el archivo al simulador**

En `sim/build.ps1`, agregar a la lista de fuentes, después de `ui_ferced.cpp`:
```powershell
    "$root\src\ui_config.cpp"
```

**Step 2: Agregar la tecla que la muestra**

En `sim/src/sim_main.cpp`, incluir `#include "ui_config.h"` y agregar en el
`switch` de teclas:

```cpp
case SDLK_c: {
    // Datos falsos: lo que se verifica aca es la disposicion, no los valores.
    static const ConfigInfo demo = {
        "1.0.0", "e96efeb-dirty", "2026-08-03 23:51",
        "MiWiFi", "192.168.1.41", "feed.ferced.com",
        8073, 35.4f, 20, true
    };
    uiConfigDraw(demo);
    break;
}
```

**Step 3: Compilar, correr y capturar**

```powershell
cd sim
.\shot.ps1 -Key c
```

Si `shot.ps1` no soporta `-Key`, correr el simulador a mano, apretar `c` y
capturar con la tecla que ya use para eso.

**Step 4: MIRAR LA CAPTURA**

Abrir `sim/build/shot.png` y revisar, en este orden:

1. ¿Entra todo sin salirse de los 480 px de alto?
2. ¿La columna de valores en `x=200` no pisa ninguna etiqueta larga?
3. ¿Los botones no se solapan y el texto entra adentro?
4. ¿El `Reaparear WiFi` se lee claramente en rojo y separado?

**Corregir la geometría acá y volver al paso 3 hasta que quede.** Es gratis:
cada vuelta son segundos. Cualquier ajuste que se descubra flasheando cuesta
minutos y depende de que el USB funcione.

**Step 5: Commit**

```bash
git add sim/build.ps1 sim/src/sim_main.cpp src/ui_config.cpp
git commit -m "sim: renderizar la pantalla de configuracion con la tecla c"
```

---

### Task 4: Cablearla en el firmware

**Files:**
- Modify: `src/ferced_main.cpp`

**Step 1: Agregar la fase**

```cpp
enum AppPhase : uint8_t {
    PHASE_PROVISION,
    PHASE_CONNECTING,
    PHASE_RUNNING,
    PHASE_CONFIG,
};
```

Y `#include "ui_config.h"` arriba.

**Step 2: Armar la struct y entrar**

Agregar antes de `loop()`:

```cpp
// El host del endpoint, sin esquema ni ruta: alcanza para distinguir el build
// de LAN del de produccion y no expone la ruta ni el token.
static const char* endpointHost() {
    static char host[48];
    const char* p = strstr(FEED_ENDPOINT, "://");
    p = p ? p + 3 : FEED_ENDPOINT;
    size_t n = 0;
    while (p[n] && p[n] != '/' && n < sizeof(host) - 1) { host[n] = p[n]; n++; }
    host[n] = '\0';
    return host;
}

static void enterConfig() {
    s_phase = PHASE_CONFIG;
    static char ipBuf[16];
    const bool online = wifiIsConnected();
    if (online) snprintf(ipBuf, sizeof(ipBuf), "%s", WiFi.localIP().toString().c_str());
    else        ipBuf[0] = '\0';

    static char ssidBuf[33];
    char passBuf[65];
    nvsLoadWifi(ssidBuf, sizeof(ssidBuf), passBuf, sizeof(passBuf));

    const UiFrameStats st = uiFercedStats();
    const ConfigInfo info = {
        FERCED_VERSION, FERCED_COMMIT, FERCED_BUILD_DATE,
        ssidBuf, ipBuf, endpointHost(),
        millis() / 1000,
        st.avgUs100 > 0 ? 10000.0f / (float)st.avgUs100 : 0.0f,
        feedCount(),
        online,
    };
    uiConfigDraw(info);
}
```

Nota: `wifiIsConnected()` puede llamarse distinto — verificar en
`src/wifi_manager.h` y ajustar.

**Step 3: Rutear el long press y los toques**

En `loop()`, dentro de la rama de `PHASE_RUNNING`, reemplazar:

```cpp
if (touchLoop().gesture == TOUCH_TAP) advance();
```

por:

```cpp
const TouchEvent ev = touchLoop();
if (ev.gesture == TOUCH_LONG_PRESS) { enterConfig(); return; }
if (ev.gesture == TOUCH_TAP) advance();
```

Y agregar, antes de esa rama, el manejo de la fase nueva:

```cpp
if (s_phase == PHASE_CONFIG) {
    const TouchEvent ev = touchLoop();
    if (ev.gesture == TOUCH_TAP) {
        switch (uiConfigHit(ev.x, ev.y)) {
            case CFG_CLOSE:
                s_phase = PHASE_RUNNING;
                showCurrent();
                s_lastRotate = millis();
                break;
            case CFG_REFRESH:
                refreshFeed();
                s_phase = PHASE_RUNNING;
                s_lastRotate = millis();
                break;
            case CFG_FORGET:
                // Sin confirmacion, por decision explicita. El boton va
                // separado y en rojo para bajar la chance de un roce.
                nvsForgetWifi();
                ESP.restart();
                break;
            default: break;
        }
    }
    delay(8);
    return;
}
```

**Step 4: Compilar**

```bash
python -m platformio run -e ferced_display_vps
```
Esperado: SUCCESS

**Step 5: Commit**

```bash
git add src/ferced_main.cpp
git commit -m "config: entrar con long press y cablear las dos acciones"
```

---

### Task 5: Verificar en el aparato — BLOQUEADA

**Requiere COM3.** Al escribir este plan el CDC del aparato está trabado y no
enumera. La secuencia para destrabarlo es BOOT apretado, RESET, soltar BOOT.

**Step 1: Flashear**

```bash
python -m esptool --chip esp32s3 --port COM3 --baud 921600 --before default-reset --after hard-reset write-flash --flash-mode keep --flash-size keep 0x10000 .pio\build\ferced_display_vps\firmware.bin
```

**Step 2: Probar a mano, en este orden**

1. Mantener apretado ~1s sobre el feed → tiene que abrir la pantalla.
2. Que el commit que muestra coincida con `git log --oneline -1`.
3. Tocar `Cerrar` → vuelve al feed.
4. Volver a entrar, tocar `Actualizar feed` → vuelve al feed; en el log del
   proxy tiene que aparecer un `[feed]` nuevo del aparato.
5. Tocar entre los botones y en los márgenes → no tiene que pasar nada.
6. **`Reaparear WiFi` último**, porque obliga a reaparear con el teléfono.

**Step 3: Confirmar que el tap simple sigue avanzando**

Un tap corto sobre el feed tiene que seguir pasando al ítem siguiente. Si el
long press se come el tap, revisar el orden de los `if` en el paso 3 de la
Task 4.

**Step 4: Commit del handoff**

Actualizar `docs/HANDOFF.md`: sacar la pantalla de configuración de pendientes,
documentar el long press y la inyección de versión.

```bash
git add docs/HANDOFF.md
git commit -m "docs: la pantalla de configuracion y como se lee la version"
```

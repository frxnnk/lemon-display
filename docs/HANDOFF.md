# ferced-display — traspaso

Estado al 2026-08-03. Rama `feat/ferced-display` del clon de `frxnnk/lemon-display`.

Leé esto entero antes de tocar nada. La mitad de los problemas de este proyecto
no se deducen del código.

---

## Qué es

Una Lemon Box (MaTouch ESP32-S3 4.0", 480x480 táctil) con firmware propio de
Ferced que muestra un feed rotativo de titulares, uno cada 17 segundos, con
miniatura y animación de entrada.

Tres piezas:

| Pieza | Dónde vive | Estado |
|---|---|---|
| **Proxy** (Go) | VPS Windows, `https://feed.ferced.com` | En producción |
| **Firmware** (C++/Arduino) | El aparato, `192.168.1.41` | Funcionando |
| **Simulador** (C++/SDL) | `sim/`, corre en la PC | Funcionando |

El firmware **no sabe de dónde salen los datos**: pide una URL y dibuja lo que
llega. Todo lo frágil —claves, formatos de terceros, limpieza de texto— vive en
el proxy, donde se arregla sin reflashear. Esa decisión pagó varias veces.

---

## Lo que está andando

**Proxy en el VPS.** Servicio `ferced-feedproxy` en `173.212.246.68`, arranque
automático, corriendo como `NT AUTHORITY\LocalService`, escuchando **sólo en
127.0.0.1:9110** detrás de Caddy. 70 tests en Go.

**Cuatro fuentes RSS** intercaladas: BBC Mundo, La Nación, BBC Tech, Xataka.

**Firmware** con animación escalonada, miniaturas de 64x64 y reconexión
automática de WiFi. 16,6% de flash, 19% de RAM.

**Simulador** que corre el mismo `src/ui_ferced.cpp` con la misma LovyanGFX
sobre SDL, reproduciendo el costo de frame medido en el aparato.

---

## Cómo trabajar

### Iterar la UI (esto es lo que más vas a hacer)

```powershell
cd sim
python tools\fetch_fixture.py     # baja contenido real del proxy, una vez
.\shot.ps1 -Advance 1             # compila, corre, captura item 1
```

La captura queda en `sim/build/shot.png`. **Mirala siempre**: dos bugs serios
se encontraron a simple vista y habrían costado horas en el aparato.

Ítems con imagen en el fixture: los índices impares (1, 3, 5, …).

### Compilar y flashear

```powershell
python -m platformio run -e ferced_display        # apunta a la LAN
python -m platformio run -e ferced_display_vps    # apunta al VPS con token
python -m esptool --chip esp32s3 --port COM3 --baud 921600 --before default-reset --after hard-reset write-flash --flash-mode keep --flash-size keep 0x10000 .pio\build\ferced_display_vps\firmware.bin
```

**Siempre `--flash-mode keep --flash-size keep`.** Forzar `qio` causa boot loop.

### Operar el proxy

```powershell
ssh -i $env:USERPROFILE\.ssh\id_ed25519_franco_vps Administrator@173.212.246.68
```

Es Windows. Logs en `C:\ferced\feedproxy\out.log`. Config en
`C:\ferced\feedproxy\start-feedproxy.bat` (wrapper `.bat`, no variables de
NSSM — ver trampas). Servicio: `ferced-feedproxy`.

---

## Secretos

**Nunca en el repo.** Verificalo con `git grep` antes de commitear.

| Secreto | Dónde |
|---|---|
| Token del feed | `output/FEED_TOKEN.txt` (ignorado) → inyectado por `tools/inject_secrets.py` |
| Clave de Sorsa | Sólo en el `.bat` del VPS |

La clave de Sorsa se pasó por chat en algún momento: **conviene rotarla**. Vive
sólo en el `.bat` del VPS.

---

## Trampas, todas encontradas a los golpes

### Del hardware

**El panel refresca a 42,3 Hz, no a 60.** `freq_write` está en 12 MHz sobre
548x518 con porches → 12e6/(548·518) = 42,3 Hz = **23,64 ms por VSync**. La
ficha de Makerfabs dice "FPS > 50" porque asume un pclk más alto. Subirlo a
16 MHz daría 56 Hz pero aumenta la contención de PSRAM, que es el cuello de
botella real. **Sin medir, no lo toques.**

**Un frame a pantalla completa cuesta ~99 ms.** El sprite vive en PSRAM;
componer + empujar son ~920 KB de tráfico. La ecuación medida es
`costo(ms) = 23,4 + 0,1575 × filas`. De ahí sale todo el diseño de bandas.

**El puerto USB se traba con algunos firmwares.** Con `ARDUINO_USB_CDC_ON_BOOT=0`
el aparato retiene el USB y esptool no puede abrirlo (`PermissionError(31)`).
Solución: BOOT + RESET a mano. Con el firmware actual no pasa.

**El puerto TTL no anda sin driver.** La placa tiene dos USB-C: el nativo (no
necesita nada) y uno por CP2104, que pide el driver VCP de Silicon Labs. Si no
está, aparece con código 28 y no enumera COM.

### Del software

**`pushImage` con `uint16_t*` asume orden intercambiado.** Las imágenes salían
con colores rotos. Hay que castear a `(const lgfx::rgb565_t*)`.

**Go pasa a `Transfer-Encoding: chunked`** cuando la respuesta supera ~4 KB sin
`Content-Length` declarado. El `HTTPClient` del ESP32 entrega ese stream **con
el framing de chunks adentro** y ArduinoJson no lo parsea. El proxy ahora
serializa a buffer y declara el largo; hay un test que falla si eso se pierde.

**El `vsync.h` del repo no funciona** — lo dice en su propia primera línea.
Incluirlo rompe el build.

**Los includes con comillas resuelven primero en el directorio del archivo.**
Por eso el simulador no podía sombrear `display_manager.h` y hubo que meter un
`#ifdef FERCED_SIM` en el firmware.

### De las fuentes de datos

**La API de sindicación de X está muerta para timelines.** Verificado el
2026-08-02: `timeline/profile`, `widgets/timelines/profile` y `timeline/list`
devuelven 200 con `Content-Length: 0`. Sólo `tweet-result?id=` sigue andando, y
sirve para renderizar un tweet conocido, no para descubrir. Requiere cookies de
sesión logueada, que se descartó por términos de uso y riesgo de la cuenta.

**X cobra desde el 2026-02-08**: USD 0,005 por post leído. El home timeline sólo
lo sirve la API oficial (requiere user context OAuth) → USD 75-225/mes.

**Sorsa** (`/v3/list-tweets`) sí cubre Listas públicas, con header `ApiKey`, sin
OAuth. El swagger público está en `https://api.sorsa.io/v3/swagger.json` — usalo,
la doc web pide login.

**El límite de body del RSS tiene que ser ≥ 1 MB.** Xataka pesa 316 KB y La
Nación 763 KB; truncarlos parte un bloque CDATA y se pierde la fuente entera.

**BBC Tech titula sus programas "Tech Now"** y pone la historia en la
descripción. Si el título tiene menos de 34 caracteres se le suma la
descripción, limpiando HTML antes.

### De Windows

**PowerShell 5.1 rompe la salida de exes nativos.** Redirigir stderr envuelve
cada línea en un ErrorRecord y se pierde. Para capturar salida de compiladores,
usar archivo de respuesta + redirección de `cmd`.

**GCC come las barras invertidas** dentro de un archivo de respuesta. Usar
barras normales.

**En un `.bat`, `|` es tubería.** `set RSS_FEEDS=a|b` se parte. Hay que
entrecomillar toda la asignación: `set "RSS_FEEDS=a|b"`.

**`AppEnvironmentExtra` de NSSM no llegó al proceso** aunque quedaba bien en el
registro. Por eso el servicio usa un wrapper `.bat`, que además es la convención
de las otras cosas que corren en ese VPS.

**El BOM de UTF-8 rompe la primera línea** de un script mandado por stdin a
`powershell -Command -`. Poner una línea vacía al principio.

---

## Pendiente

### Inmediato: el framerate

Es lo que estaba en curso. Medido con telemetría real del aparato:

| Cambio | Por frame | FPS |
|---|---|---|
| Original | 98,8 ms | 10 |
| Empujar sólo la banda sucia | 78,4 ms | 13 |
| Un slot por renglón | 45,6 ms | 22 |
| Slots en secuencia | 69,4 ms | 14 ↓ |
| Arreglar el hueco entre slots | **36,0 ms** | **28** |

El techo del panel son 42 fps. Faltan dos palancas: **acortar la animación**
para que entre en menos frames, y **subir `freq_write` a 16 MHz**.

El firmware manda `fr`, `avg` y `max` como parámetros en cada pedido de imagen
(cada ~35 s), y el proxy los registra como `[anim]`. Ahí se mide.

### Funcionalidad

**La Lista de X.** Era el pedido original. El código está escrito y testeado
contra el esquema oficial (`proxy/internal/sorsa/`), en `SKIP` hasta que exista
`testdata/list_tweets.json`. Falta que el usuario cree una Lista **pública** y
pase el ID numérico. Se enchufa con `SORSA_LIST_ID` en el `.bat` del VPS, sin
reflashear.

**Modo Archillect.** Pedido: al tocar el logo, mostrar contenido de
archillect.com hasta que el usuario cierre. Son imágenes a pantalla completa, o
sea 480x480 RGB565 = 460 KB por imagen, contra los 8 KB de las miniaturas. Otro
pipeline. **Verificar primero si la API existe** — con X nos salvó de construir
sobre algo muerto.

**Fuente Latin-1.** Hoy el proxy translitera y se lee "anos" en vez de "años".
Arreglo real: regenerar la fuente con `tools/ttf_to_gfx.py`.

**Renombrar el AP de provisioning.** Sigue diciendo `Lemon-Setup` hardcodeado en
`wifi_provision.cpp:16`; `FERCED_AP_SSID` está definido pero sin cablear.

---

## Decisiones que conviene no revertir

**El firmware no conoce las fuentes.** Cambiar de RSS a Sorsa, arreglar el
parseo de títulos o sumar feeds no requirió reflashear ni una vez.

**El proxy resuelve las imágenes.** Baja, recorta a cuadrado, escala promediando
por área y convierte a RGB565. El ESP32 sólo pinta píxeles. Además el aparato
nunca manda una URL, sólo una key ya resuelta: no hay SSRF.

**El simulador usa el archivo real, no una copia.** Un `#ifdef` en
`display_manager.h` en vez de duplicar `ui_ferced.cpp`. Una copia se
desincroniza y el simulador empieza a mentir.

**Medir antes de optimizar.** El paso "slots en secuencia" empeoró el framerate
y sólo se supo por la telemetría. Sin medir se habría quedado el bug adentro.

---

## Archivos que importan

| Ruta | Qué es |
|---|---|
| `src/ui_ferced.cpp` | Toda la UI y la animación |
| `src/ferced_main.cpp` | Máquina de estados: provisioning, WiFi, rotación |
| `src/feed_client.cpp` | Cliente HTTP del feed y las imágenes |
| `proxy/internal/` | Fuentes, mezclador, normalizador, imágenes, guard |
| `sim/shot.ps1` | Compilar + correr + capturar, un comando |
| `docs/plans/2026-08-02-ferced-display-design.md` | Diseño validado |
| `docs/plans/2026-08-02-ferced-display.md` | Plan de implementación |
| `output/release-beta74-*/ROLLBACK.md` | Cómo volver al firmware de fábrica |

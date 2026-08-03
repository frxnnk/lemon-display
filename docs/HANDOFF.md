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
automática de WiFi. 16,6% de flash, 19% de RAM. Animación a 35,5 fps medidos
sobre un techo de panel de 42.

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

### Leer la telemetría

Al terminar cada transición (una cada 17 s) el firmware imprime por serie:

```
[anim] frames=45 cost=22.7ms max=35.4ms period=28.0ms fps=35.7 waits=48 timeouts=0
```

`cost` es lo que tarda `uiFercedTick()`; `period` es el reloj de pared entre
frames y **de ahí sale el FPS de verdad**. `timeouts` es el dato que delata si
`displayWaitVSync()` está sincronizando o agotando su timeout: si `timeouts`
iguala a `waits`, la ISR de VSync no está registrada (ver trampas).

El aparato retiene el USB nativo, así que `platformio device monitor` pelea con
esptool. Para capturar sin bloquear la terminal:

```powershell
$p = New-Object System.IO.Ports.SerialPort 'COM3',115200,'None',8,'one'
$p.ReadTimeout = 1500; $p.DtrEnable = $true; $p.Open()
$fin = (Get-Date).AddSeconds(60)
while ((Get-Date) -lt $fin) { try { $p.ReadLine() } catch { } }
$p.Close()
```

Resetear con esptool cierra y reabre el puerto (re-enumera), así que para ver el
arranque hay que reabrir en bucle hasta que aparezca de nuevo.

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

Desde el 2026-08-03 ya no es una deducción: `displaySetupVSync()` cuenta ticks
de VSync durante 500 ms al arrancar e imprime `[Display] ... panel at 42.0 Hz
(21 ticks / 500 ms)`. Con 21 ticks la cuantización es de ±2 Hz, así que
confirma la cuenta de arriba sin contradecirla.

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

**Compartir un vector de interrupción exige coincidir en el flag de IRAM.**
Costó 12 fps y estuvo escondido desde el principio. `displaySetupVSync()` pedía
`ESP_INTR_FLAG_SHARED | ESP_INTR_FLAG_IRAM`, pero LovyanGFX registra ese mismo
vector con `ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED`, **sin** IRAM
(`Bus_RGB.cpp`, esp32s3). ESP-IDF exige que todos los handlers de un vector
compartido coincidan en ese flag; como no coincidían y la fuente ya estaba
ruteada, no quedaba slot y devolvía `ESP_ERR_NOT_FOUND` (261). El resultado era
silencioso y caro: `_vsync_count` congelado en cero y **cada**
`displayWaitVSync()` agotando sus 25 ms de timeout. El handler sigue con
`IRAM_ATTR` —eso es lo que pone el código en IRAM—; el flag sólo declaraba un
requisito que rompía el sharing.

Moraleja general: un `esp_err_t` que se imprime y se sigue de largo puede costar
un tercio del framerate. Si algo devuelve error al arrancar, no lo dejes pasar
porque "igual anda".

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

**Invocar `powershell` remoto por ssh al VPS no devuelve salida.** Verificado el
2026-08-03: `ssh ... hostname` anda perfecto y devuelve `vmi3426337`, pero
cualquier `ssh ... "powershell -NoProfile -Command ..."` termina en
`Terminate batch job (Y/N)?` sin imprimir nada. Falla igual con `-n -T`, con
`-EncodedCommand` (que descarta el problema de comillas) y con un wrapper `.bat`
local redirigiendo a archivo — o sea que es del lado remoto, no del quoting.
El mensaje llega **por el canal de ssh**, así que lo emite el VPS. Sin
diagnosticar. Mientras tanto, para medir el framerate usá la telemetría por
serie en vez del log del proxy.

---

## Pendiente

### Inmediato: el framerate

Historial de optimizaciones, medido con telemetría real del aparato:

| Cambio | Costo por frame | FPS (1000/costo) |
|---|---|---|
| Original | 98,8 ms | 10 |
| Empujar sólo la banda sucia | 78,4 ms | 13 |
| Un slot por renglón | 45,6 ms | 22 |
| Slots en secuencia | 69,4 ms | 14 ↓ |
| Arreglar el hueco entre slots | 36,0 ms | 28 |
| **Registrar la ISR de VSync** | **22,7 ms** | — |

**Cuidado con esa columna de FPS: está inflada.** Sale de `1000/costo`, y el
costo mide sólo lo que pasa dentro de `uiFercedTick()`: deja afuera el
`delay(1)` del loop, el sondeo del táctil y `wifiLoop()`. Desde el 2026-08-03 el
instrumento reporta las dos cosas por separado —`cost` y `period`— y el FPS sale
del período real de reloj de pared.

Con la vara nueva, A/B sobre el mismo firmware reintroduciendo el flag roto a
propósito (5 muestras contra 4, dispersión ±0,3 fps):

| | cost | period | FPS | timeouts |
|---|---|---|---|---|
| ISR de VSync fallando | 36,4 ms | 43,1 ms | 23,2 | 100 % |
| ISR de VSync andando | 22,7 ms | 28,0 ms | **35,5** | 0 % |

O sea que el "28 fps" que figuraba acá como mejor marca en realidad eran 23,2.

**La palanca que queda.** El período es 28,0 ms y el del panel 23,6 ms. Como
ahora toda espera termina en un flanco real, el período debería ser un múltiplo
exacto de 23,6 — y no lo es. La lectura: ~18 % de los frames se pasan del
presupuesto y esperan al flanco siguiente (`28,0 / 23,6 = 1,18`). Recortar un
par de ms del trabajo por vuelta —los ~5,4 ms que viven fuera del tick, o el
push— mete esos frames en el balde de un período y empuja hacia los 42 fps del
techo. Es inferencia sobre las medias; la distribución por frame no está medida.

Subir `freq_write` a 16 MHz sigue siendo la otra palanca, con la misma
advertencia de siempre: sin medir, no.

El firmware manda `fr`, `avg` y `max` en cada pedido de feed y el proxy los
registra como `[anim]`, pero **no hace falta el log remoto**: la misma línea sale
por serie al terminar cada transición. Ver "Leer la telemetría".

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

**Y desconfiá del instrumento también.** Al arreglar la ISR de VSync el
instrumento marcó 45 fps sobre un panel de 42: imposible, y la pista de que
medía mal. Medía sólo el costo dentro del tick. El número real era 35,5. Un
resultado que **supera** el techo teórico es tan sospechoso como uno que no
llega — en los dos casos, andá a buscar el dato crudo.

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

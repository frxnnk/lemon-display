# ferced-display — traspaso

Estado al 2026-08-05. Rama `feat/ferced-display` del clon de `frxnnk/lemon-display`.

Si venís a **mejorar la interfaz**, andá directo a `docs/HANDOFF-UI.md`: ahí está
el presupuesto de frame, la identidad, la geometría de cada pantalla y el ciclo
del simulador. Este documento cubre el resto del sistema.

Leé esto entero antes de tocar nada. La mitad de los problemas de este proyecto
no se deducen del código.

## Si venís a retomar: las tres cosas que importan hoy

1. **El OTA funciona: ya corrió dos veces de verdad.** La 1.1.0 y la 1.1.1 se
   publicaron y se instalaron desde el botón, sin USB. Eso cambia el proyecto:
   actualizar dejó de depender del conector más frágil del aparato. Lo que
   **sigue sin probarse es el rollback** —la red que atrapa un firmware que
   arranca y se queda sin red—, y esa prueba no es opcional. Ver "Verificar el
   OTA".
2. **La fuente de X está caída por cuota agotada.** El feed sirve sólo RSS. Ver
   "La cuota de Sorsa".
3. **El USB de este aparato es poco confiable.** Ver las trampas del hardware
   antes de pelearte con él una hora, como ya pasó.

---

## Qué es

Una Lemon Box (MaTouch ESP32-S3 4.0", 480x480 táctil) con firmware propio de
Ferced. Desde la 1.3.0 tiene **cuatro apps**: noticias (un titular cada 17 s,
con miniatura), pádel (el orden de juego del circuito profesional), tareas (una
lista que se edita desde el teléfono) y avisos (los agentes de Claude y Codex
avisan cuando terminan). Se pasa de una a otra deslizando, o desde un selector.

Tres piezas:

| Pieza | Dónde vive | Estado |
|---|---|---|
| **Proxy** (Go) | VPS Windows, `https://feed.ferced.com` | En producción |
| **Firmware** (C++/Arduino) | El aparato, `http://ferced.local/` | Funcionando |
| **Simulador** (C++/SDL) | `sim/`, corre en la PC | Funcionando |

El firmware **no sabe de dónde salen los datos**: pide una URL y dibuja lo que
llega. Todo lo frágil —claves, formatos de terceros, limpieza de texto— vive en
el proxy, donde se arregla sin reflashear. Esa decisión pagó varias veces.

---

## Lo que está andando

**Proxy en el VPS.** Servicio `ferced-feedproxy` en `173.212.246.68`, arranque
automático, corriendo como `NT AUTHORITY\LocalService`, escuchando **sólo en
127.0.0.1:9110** detrás de Caddy. 109 tests en Go.

**Cuatro fuentes RSS** intercaladas: BBC Mundo, La Nación, BBC Tech, Xataka.

**Una fuente de X por búsqueda**, hoy **caída** (ver "La cuota de Sorsa"). Cuando
anda aporta ~4 de los 20 ítems, todos con imagen real del tweet.

**App de pádel**, en `/v1/padel`. Ver "La app de pádel".

**App de tareas**, servida por el propio ESP32 en el puerto 80. Ver "La app de
tareas".

**Avisos de los agentes**, en `POST /api/notify` del mismo servidor. Ver "Los
avisos".

**OTA por WiFi, probado en el aparato.** El proxy sirve `/v1/firmware` y
`/v1/firmware/bin` desde `C:\ferced\firmware`, y el aparato tiene el botón
«Buscar actualización». La 1.1.0 y la 1.1.1 se instalaron así, sin tocar el USB.
Ver "El OTA".

**Firmware** con animación escalonada, miniaturas de 64x64 y reconexión
automática de WiFi. 19,8% de flash, 24,6% de RAM. Animación a 35,5 fps medidos
sobre un techo de panel de 42.

**Los gestos**, que ahora son cinco:

| Gesto | Qué hace |
|---|---|
| Toque | siguiente ítem dentro de la app; en tareas, marca la que se tocó |
| Deslizar ← o → | cambiar de app |
| Deslizar ↑ | abrir el selector de apps |
| Deslizar ↓ (en el selector) | cerrarlo sin cambiar |
| Pulsación larga | pantalla de configuración |

**Selector de apps**: una tarjeta por app, con monograma, nombre y estado en
vivo («20 titulares», «LONDON P1 · día 4 de 8», «5 de 7 pendientes»). La app
corriendo se dibuja en negativo. **No se anima, a propósito**: las tarjetas son
altas y la banda sucia terminaría siendo la pantalla entera, o sea ~99 ms por
frame. Se dibuja de una sola vez, como la de configuración.

La grilla se calcula a partir de cuántas apps hay. Estuvo clavada para dos, y al
sumar la tercera se salía de la pantalla —140 + 2*(104+20) + 104 = 492 sobre un
panel de 480— tapando los puntos y el pie. Agregar una app no debería obligar a
rehacer la geometría.

**Pantalla de configuración**: se abre con un **long press** en cualquier parte.
Muestra versión, commit y fecha de compilación —inyectados al compilar por
`tools/inject_version.py`, con sufijo `-dirty` si el árbol estaba sucio—, más
red, IP, host del feed, uptime, fps e ítems en el pool. Trae cuatro botones:
actualizar feed, buscar actualización, cerrar y reaparear WiFi (este último
borra las credenciales **sin confirmación**, por decisión explícita). Diseño y
plan en `docs/plans/2026-08-03-pantalla-configuracion*.md`.

Es la respuesta rápida a "¿la cajita tiene lo último?": comparás el commit de la
pantalla contra `git log --oneline -1`.

**Simulador** que corre el mismo `src/ui_ferced.cpp` con la misma LovyanGFX
sobre SDL, reproduciendo el costo de frame medido en el aparato.

---

## Cómo trabajar

### Iterar la UI (esto es lo que más vas a hacer)

```powershell
cd sim
python tools\fetch_fixture.py     # baja contenido real del proxy, una vez
.\shot.ps1 -Advance 1             # compila, corre, captura item 1
.\shot.ps1 -Padel -Advance 1      # pádel: 0 es el torneo, 1.. los partidos
.\shot.ps1 -Tareas                # la lista de tareas (fixture: sim/data/todo.txt)
.\shot.ps1 -Aviso                 # la tarjeta que interrumpe
.\shot.ps1 -Avisos                # la lista de avisos (fixture: sim/data/avisos.txt)
.\shot.ps1 -Launcher              # el selector de apps
.\shot.ps1 -Config                # la pantalla de configuración
```

La captura queda en `sim/build/shot.png` (o en `-Out loquesea.png`). **Mirala
siempre**: seis bugs serios se encontraron a simple vista y habrían costado
horas en el aparato, y ninguno daba error de compilación. Dos fueron
tipográficos —la «í» de «día» y el punto medio del chip, fuera del rango de la
fuente— y dos de desborde del selector al sumar apps.

Ítems con imagen en el fixture: los índices impares (1, 3, 5, …).

**Para mirar una transición por dentro**, que es donde vive lo que hay que
verificar:

```powershell
.\shot.ps1 -Advance 3 -Hacia 1 -Congelar 420    # muestra el 3, pasa al 1, congela a los 420 ms
```

Lo que tiene que verse: el titular nuevo entrando arriba **y el pie del anterior
todavía en pantalla**. Si en algún momento la pantalla queda vacía, volvió el
parpadeo. Y `-Congelar 2000` de un ítem largo a uno corto verifica que no queden
renglones huérfanos colgados.

El fixture de pádel se genera aparte, con datos del circuito real:

```powershell
cd proxy
go run ./cmd/padelcheck -fixture ../sim/data/padel.txt
```

Que sean nombres de verdad importa: la pantalla se rompe justamente con los
apellidos largos («S. Pineda Cabello»), y un fixture inventado los escondería.
Para probar un partido **ya jugado** —otro camino de dibujo, con ganador,
perdedor en gris y resultado a la derecha— hay que insertar una línea a mano
como primer `M|`: temprano a la mañana en Londres todavía no se jugó ninguno, y
sólo se cargan los primeros `PADEL_MAX_MATCHES` del archivo.

### Compilar y flashear

```powershell
python -m platformio run -e ferced_display        # apunta a la LAN
python -m platformio run -e ferced_display_vps    # apunta al VPS con token
python -m esptool --chip esp32s3 --port COM3 --baud 921600 --before default-reset --after hard-reset write-flash --flash-mode keep --flash-size keep 0x10000 .pio\build\ferced_display_vps\firmware.bin
```

**Siempre `--flash-mode keep --flash-size keep`.** Forzar `qio` causa boot loop.

**El env Lemon original (`matouch_esp32s3_40`) ya no linkea.** Verificado el
2026-08-03 sobre HEAD limpio: falla con `multiple definition of setup()` y
`loop()` entre `main.cpp` y `ferced_main.cpp`. La causa es que ese env no tiene
`build_src_filter`, así que compila todo `src/` — incluido el `ferced_main.cpp`
que se agregó después. Está roto desde que existe el firmware de Ferced, no es
una regresión reciente. Se arregla excluyendo `ferced_main.cpp` de ese env; no se
hizo porque nadie lo estaba usando. Tenelo en cuenta si tocás código compartido
(`display_manager.cpp`, `wifi_provision.cpp`, `ui_components.cpp`): el gate
`#ifdef FERCED_DISPLAY` es correcto, pero la rama Lemon no se puede compilar para
probarla.

### Leer la telemetría

Al terminar cada transición (una cada 17 s) el firmware imprime por serie:

```
[anim] frames=45 cost=22.7ms max=35.4ms period=28.0ms fps=35.7 waits=48 timeouts=0
```

`cost` es lo que tarda `uiFercedTick()`; `period` es el reloj de pared entre
frames y **de ahí sale el FPS de verdad**. `timeouts` es el dato que delata si
`displayWaitVSync()` está sincronizando o agotando su timeout: si `timeouts`
iguala a `waits`, la ISR de VSync no está registrada (ver trampas).

El `period` descarta los huecos de más de 250 ms a propósito. Sin ese filtro, un
refresco del feed que caiga en medio de una animación se cuenta como si fuera un
frame lentísimo: se midió `period=1542ms, fps=0.6` con la pantalla andando a 35.
Un hueco así no es render, es el loop bloqueado en HTTPS.

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

### El OTA

Construido el 2026-08-04, desplegado, **sin verificar en el aparato**. Diseño en
`docs/plans/2026-08-04-ota-design.md`, plan en `2026-08-04-ota.md`.

**Cómo funciona.** El aparato pide `/v1/firmware` al proxy —con el mismo Bearer
token que el feed—, compara la versión con `FERCED_VERSION` y, si hay una mayor,
descarga `/v1/firmware/bin` a la partición inactiva. Se dispara a mano desde la
pantalla de configuración; no chequea solo.

**Publicar una versión nueva:**

```powershell
# 1. Subir VERSION en tools/inject_version.py y COMMITEAR antes de compilar
python -m platformio run -e ferced_display_vps
# 2. Subir el binario y la version al VPS
scp -i $env:USERPROFILE\.ssh\id_ed25519_franco_vps .pio\build\ferced_display_vps\firmware.bin Administrator@173.212.246.68:C:/ferced/firmware/firmware.bin
# 3. version.txt tiene que decir lo mismo que VERSION
```

**Compilá siempre con el árbol limpio.** Con cambios sin commitear el sello queda
`-dirty` y el aparato termina mostrando un commit que no existe, que es
exactamente la confusión que el sello existe para evitar. Ya pasó dos veces y
hubo que republicar las dos.

**La segunda vez fue por el orden de los pasos, no por olvidarse de commitear.**
`proxy/feedproxy-vps.exe` está versionado, así que reconstruirlo para desplegar
el proxy ensucia el árbol. Si eso pasa *después* de commitear el firmware, el
build siguiente sale `-dirty` aunque uno haya commiteado todo. El orden que
funciona: **construir y commitear el exe del proxy primero, y recién después
compilar el firmware.** Y verificar antes de publicar:

```powershell
python -c "import re; s=open(r'.pio\build\ferced_display_vps\firmware.bin','rb').read(); print('dirty' if re.search(rb'[0-9a-f]{7}-dirty', s) else 'limpio')"
```

**Tres decisiones que conviene no revertir:**

- **La descarga valida el certificado** (`setCACert(LE_ROOTS)`), a diferencia del
  feed, que usa `setInsecure()`. Para noticias públicas el token ya autentica y
  el contenido no es ejecutable; un binario sí lo es. Van las dos raíces de Let's
  Encrypt porque el servidor sirve una cadena u otra según lo que negocie el
  cliente.
- **`Update.end()` se llama recién después de verificar el sha256.** Es lo que
  marca la partición nueva como arrancable: hacerlo antes sería dejar el aparato
  apuntando a un binario que no se sabe qué es.
- **La confirmación del arranque no va al bootear.** Ver abajo.

**La red de rollback, y la trampa de Arduino.** Con `CONFIG_APP_ROLLBACK_ENABLE`
el bootloader deja la partición nueva en `PENDING_VERIFY`: si nadie la confirma,
el próximo arranque vuelve sola a la anterior.

Arduino la anulaba. En `initArduino()` confirma apenas bootea, con lo cual un
firmware que arranca y se queda sin red quedaba fijado igual — justo el caso que
la red tiene que atrapar. Esa llamada está envuelta en
`if(!verifyRollbackLater())`, y esa función es **débil**: `ferced_main.cpp` la
define fuerte devolviendo `true` para que Arduino se abstenga. La confirmación se
da en `refreshFeed()`, cuando el primer feed vuelve con éxito.

Un flasheo por USB **no** deja la partición en `PENDING_VERIFY`, así que después
de flashear a mano no vas a ver `[OTA] arranque confirmado` por serie. Eso es
correcto, no un bug.

**El log de `[fw]` no anota el user agent**, a diferencia del de `[feed]`. Como
el aparato y la PC de casa salen por la misma IP pública, en el log **no se
distingue un pedido del aparato de uno hecho a mano con curl o PowerShell**. Eso
arruinó un diagnóstico el 2026-08-05: se contaron pedidos creyendo que eran del
aparato y varios eran propios. Agregar el user agent a esa línea es de una línea
y vale la pena antes de la próxima sesión de depuración.

### Verificar el OTA — lo que falta y no es opcional

De las dos pruebas, la primera ya pasó y la segunda —la que importa— sigue
pendiente:

1. **Camino feliz: HECHO.** La 1.1.0 y la 1.1.1 se publicaron y se instalaron
   desde el botón, con el aparato mostrando la versión nueva después. El OTA
   dejó de ser una suposición. El log del proxy ahora anota el user agent, así
   que se puede confirmar que el pedido salió del aparato y no de la PC.
2. **Rollback a propósito.** Compilar un firmware con versión mayor y el SSID
   roto adrede, publicarlo, aplicarlo, y confirmar que el aparato arranca, no
   consigue red y **vuelve solo al anterior**.

**Si la segunda no pasa, frená y no confíes en el OTA.** Un firmware malo sin
rollback deja el aparato sin ninguna vía de entrada salvo el USB, que en este
aparato es justamente lo poco confiable. Sin esa prueba, la red de seguridad es
una suposición.

### La app de pádel

Muestra el circuito profesional: el torneo que se está jugando, su orden de
juego del día —hora, cancha, fase, las dos parejas, cabeza de serie y
resultado— y los próximos torneos con cuenta regresiva. Una pantalla por
partido, con el mismo ritmo de 17 s que las noticias.

**Las dos fuentes son públicas y vienen renderizadas del lado del servidor**, así
que no hace falta clave ni ejecutar JavaScript. Eso no fue suerte: se probaron
antes las alternativas y ninguna servía. Sofascore devuelve 403 a cualquier
cliente que no sea un navegador (le mira la huella TLS, no el User-Agent);
premierpadel.com no tiene calendario en `/calendar`; padelapi.org tiene plan
gratuito pero exige crear una cuenta. El camino que quedó:

```
padelfip.com/es/calendario/?events-year=YYYY   449 torneos del año, en HTML
padelfip.com/es/eventos/<slug>/                 la página del torneo
  └─ trae get-oop-data.php?year=&id=&day=&totalday=
       └─ devuelve JSON con usedDay y la URL exacta del widget
widget.matchscorerlive.com/screen/oopbyday/FIP-<año>-<id>/<día>
                                                el orden de juego, en HTML
```

**El id del widget no se deduce del slug**: hay que leerlo de la página del
torneo. Y el `day` del HTML puede ser de ayer si la página está cacheada, por
eso se consulta el endpoint PHP, que devuelve el `usedDay` real.

`matchscorerlive` es el proveedor que usa la propia FIP para sus resultados en
vivo, así que es el mismo dato que muestra el sitio oficial.

**Se filtra a "las mejores ligas"**: Premier Padel entero (Major, Master Finals,
P1, P2) más platinum y gold del Cupra FIP Tour. Los promises, bronze y silver
son ~380 de los 449 torneos del año y taparían al circuito grande. Se cambia
con `PADEL_CATS` en el `.bat`, sin tocar código. `PADEL=0` apaga la app.

**World Padel Tour ya no existe** como circuito separado: se fusionó con Premier
Padel en 2024. Lo que hoy es "el mejor pádel" está todo en el calendario de la
FIP, que es justamente el que se lee.

TTL propios: el calendario 6 horas (pesa 2 MB y cambia de mes en mes), el orden
de juego 5 minutos. Y el aparato **sólo pide pádel con la app abierta**: fuera
de ella nadie mira el orden de juego, y son dos sitios ajenos los que pagan el
pedido.

**Los nombres completos salen de una tercera fuente.** El orden de juego sólo da
la inicial («A. Tapia»); el cuadro que publica padelfip en la página del torneo
trae el nombre entero («Agustin Tapia»). Se cruzan por la clave
`inicial + apellidos`, que es lo único que comparten —no hay ningún id común—, y
se sustituye **por pareja: los dos o ninguno**. Media pareja con nombre completo
y la otra abreviada se lee como un error, no como un dato incompleto.

El límite de 28 caracteres está **medido en el simulador**, no calculado:
«Santiago Jose Pineda Cabello» son 28 y terminan a ~85 px de la cabeza de serie.
Los que se pasan dejan a su pareja en la forma corta.

**Sólo hay nombres completos del cuadro masculino.** Verificado: la página del
torneo no trae ni una jugadora en el HTML (se probó con seis apellidos del orden
de juego femenino), y el widget de cuadros de matchscorerlive —que sí existe, en
`/screen/draw/FIP-<año>-<id>`— usa iniciales igual que el orden de juego. Así que
los partidos femeninos se quedan abreviados. Lo mismo pasa con algunos
clasificados, que no están en el cuadro principal.

**La trampa que costó el rato**: el widget escribe `class="ml-2  line-thin"`,
con **dos espacios**. El patrón que buscaba un espacio no enganchaba nada y los
partidos se perdían enteros y en silencio. Peor: la inspección previa había
colapsado el whitespace para leerla más cómoda, así que el bug estaba escondido
en el instrumento. Todos los patrones matchean las clases por contenido
(`[^"]*line-thin[^"]*`) justamente por eso.

### La app de tareas

Una lista de pendientes que se edita desde el teléfono, en
**`http://<ip-del-aparato>/`** — hoy `http://192.168.1.41/`.

**Es la única parte del sistema que NO pasa por el proxy, y es a propósito.** La
regla «todo lo frágil vive en el proxy» existe para las fuentes de terceros:
claves, formatos ajenos, limpieza de texto. Una lista de tareas no es nada de
eso. Es estado privado, tiene que poder editarse con el VPS caído, y los datos
no tienen por qué salir de la red de casa. Por eso el servidor lo levanta el
propio ESP32.

| Pieza | Dónde |
|---|---|
| Lista y persistencia | `src/todo_store.cpp`, en NVS, namespace `ferced_todo` |
| Servidor y página | `src/todo_server.cpp`, puerto 80, `ESPAsyncWebServer` |
| Pantalla | `src/ui_todo.cpp` |

La página va entera en PROGMEM, sin CDN ni fuentes externas: tiene que servirse
sin internet. Usa la tipografía del sistema porque embeber Satoshi serían ~100
KB de flash para algo que se abre de vez en cuando. Toda la app suma **31 KB de
flash y ~1 KB de RAM**.

**El namespace de NVS es propio y no el `lemon` del resto**, así que un
`nvsFactoryReset()` del código heredado no se lleva la lista puesta.

**Cuatro cosas que no son obvias:**

- **Las mutaciones exigen el encabezado `X-Ferced`.** No es autenticación
  —cualquiera en la red de casa puede editar, igual que el portal cautivo— sino
  la defensa contra CSRF: un navegador no deja que una página de otro sitio
  mande un encabezado propio sin un preflight de CORS, que este servidor no
  contesta. Sin eso, cualquier web que el usuario visite podría borrarle las
  tareas con un `<form>` escondido.
- **Dos tareas del RTOS tocan la lista**: el servidor corre en la de AsyncTCP y
  la pantalla en la del loop. El array está protegido con un `portMUX`, y la
  escritura a flash queda **fuera** de la sección crítica, que es donde no puede
  estar. La pantalla no la repinta el servidor: se compara `todoRevision()` en
  el loop. Dos tareas dibujando sobre el mismo panel es una carrera.
- **El tick tiene que ser el de la app que corre.** El de noticias repinta la
  línea de progreso cuatro veces por segundo; llamarlo con la lista en pantalla
  la va pisando. En el loop hay un `switch` por app justamente por esto.
- **El puerto 80 lo comparte con el portal cautivo** del aprovisionamiento, así
  que `startProvisioning()` baja el editor antes de levantarlo.

Para iterar la página sin flashear, `scratchpad/extraer_pagina.py` la saca del
`.cpp` y le enchufa un simulacro de la API para abrirla en el navegador. Sacarla
del `.cpp` y no tener una copia es a propósito: una copia se desincroniza, igual
que pasaría con `ui_ferced.cpp` y el simulador.

### Los avisos

Los agentes —Claude Code, Codex, o cualquier cosa que sepa hacer un pedido
HTTP— avisan a la cajita cuando terminan una tarea. El aviso **interrumpe** lo
que haya en pantalla durante 25 segundos, con una barra que se agota, y
cualquier gesto lo cierra. Después queda en la lista de la app AVISOS.

```
POST http://ferced.local/api/notify?src=claude&t=Termin%C3%B3&b=detalle
GET  http://ferced.local/api/notify?src=claude&t=...        (también sirve)
```

**Ni el POST ni el GET exigen el encabezado `X-Ferced`, a diferencia del resto
de la API.** Es deliberado y es la decisión central de todo esto: quien va a
llamar a esta URL es un hook de una línea que se instala una vez y se olvida, y
pedirle una bandera de más es fricción que se paga todos los días. El riesgo
que se acepta es que una web del navegador haga aparecer un cartel; el de que
borre la lista de tareas, no, y por eso ahí el encabezado sigue.

Se acepta **GET además de POST** por lo mismo: hay entornos que sólo saben
pedir una URL.

**Los avisos no se persisten.** Uno de "terminó la compilación" de antes de un
reinicio no le sirve a nadie: lo que importa de una notificación es que llegue
ahora. Guardarla en flash sería gastar escrituras para mostrar ruido.

**El aviso interrumpe sólo desde `PHASE_RUNNING`.** El chequeo vive después del
`switch` de fases a propósito: si estuviera antes, un aviso podría aparecer en
medio de una descarga de firmware o del aprovisionamiento.

**mDNS** para que la dirección no dependa del DHCP: el aparato responde a
`ferced.local` además de a su IP. Un hook que se instala una vez no puede
romperse porque el router cambió de humor. Si mDNS no levanta, el log lo dice y
queda la IP.

Cómo se engancha desde Claude Code, en `~/.claude/settings.json`:

```json
{
  "hooks": {
    "Stop": [{ "hooks": [{ "type": "command",
      "command": "curl -s -m 2 --get --data-urlencode 'src=claude' --data-urlencode 'Termino la tarea' --data-urlencode \"b=$(basename \\\"$PWD\\\")\" http://ferced.local/api/notify || true" }] }]
  }
}
```

El `|| true` no es adorno: si la cajita está apagada, el hook no puede hacer
fallar el turno del agente.

### La cuota de Sorsa

**Estado al 2026-08-05: agotada.** La fuente de X devuelve 403 en cada refresco,
con `{"message":"request limit exceeded"}`. Hasta `/v3/key-usage-info` responde
403, así que no se puede consultar cuánto queda ni cuándo se repone.

El mezclador degrada bien: el feed sigue sirviendo 20 ítems, todos de RSS.

**La causa es el diseño, no un accidente.** El mixer tiene **un solo TTL para
todas las fuentes** (`poolTTL`, 10 minutos), así que la búsqueda de X sale
**144 veces por día**. Antes del 2026-08-04 el proxy no le pegaba a Sorsa ni una
vez: `SORSA_LIST_ID` nunca estuvo configurado y la fuente de tendencias no está
activa.

**El arreglo propuesto y no implementado:** un envoltorio que implemente
`feed.Source`, guarde su propio TTL y devuelva lo cacheado mientras no venza.
Con una hora serían 24 por día, y en la práctica no se nota: los tweets de la
NASA no cambian cada diez minutos. Son pocas líneas y no toca el mixer.

Falta saber el límite real del plan y cada cuánto se repone. Eso lo tiene que
mirar el usuario en su cuenta de Sorsa.

### Operar el proxy

```powershell
ssh -i $env:USERPROFILE\.ssh\id_ed25519_franco_vps Administrator@173.212.246.68
```

Es Windows. Logs en `C:\ferced\feedproxy\out.log`. Config en
`C:\ferced\feedproxy\start-feedproxy.bat` (wrapper `.bat`, no variables de
NSSM — ver trampas). Servicio: `ferced-feedproxy`.

Variables que lee el proxy:

| Variable | Qué hace |
|---|---|
| `FEED_ADDR` | Dónde escucha. Default `127.0.0.1:9110` |
| `FEED_TOKEN` | Exigido en `Authorization: Bearer` |
| `RSS_FEEDS` | `url\|etiqueta` separados por coma |
| `SORSA_KEY` | Clave de Sorsa. Sin ella no hay ninguna fuente de X |
| `SORSA_QUERY` | Consulta de búsqueda avanzada, ej. `from:NASA OR from:esa` |
| `SORSA_ORDER` | `latest` (default) o `popular` |
| `SORSA_MEDIA_ONLY` | `0` deja pasar tweets sin imagen. Default: los filtra |
| `SORSA_LIST_ID` | ID de una Lista pública, si alguna vez existe |
| `TRENDS_WOEID` | `woeid\|región` separados por coma |

Acordate de entrecomillar toda la asignación cuando el valor lleve `|`
(`set "RSS_FEEDS=a|b"`), o el `.bat` lo parte como tubería.

**Para correr algo remoto usá `cmd`, no `powershell`.** `ssh ... "cmd /c ..."`
funciona; invocar `powershell` remoto devuelve `Terminate batch job` sin salida.
Para scripts más largos, `scp` un `.bat` y ejecutalo con `cmd /c`.

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

**El CDC nativo se traba y COM3 deja de abrirse.** Pasó el 2026-08-03 después de
muchos ciclos de flasheo. El síntoma es en dos etapas y conviene distinguirlas:

- `Se agotó el tiempo de espera del semáforo` o un `SerialPort.Open()` que
  **cuelga sin volver**: es el CDC del aparato, que dejó de responder. El puerto
  sigue enumerando con `Status OK` y código de error 0, así que Windows no
  ayuda a diagnosticarlo.
- `PermissionError(13, Acceso denegado)`: ya es otro proceso del host reteniendo
  el handle — típicamente un `Open()` colgado de un intento anterior.

`tools/restart_usb.ps1` deshabilita y rehabilita el dispositivo, pero
`Disable-PnpDevice` **necesita admin** y sin permisos falla en silencio (el
script usa `-ErrorAction SilentlyContinue`). El aparato mientras tanto sigue
funcionando perfecto: se confirma mirando los `[anim]` del log del proxy, que
llegan igual porque van por WiFi.

**Y no es el chip: es el contacto.** El 2026-08-04 la cosa escaló a seis
combinaciones de cable y puerto antes de volver. Vale la pena el mapa, porque
enseña a leer los síntomas:

| Combinación | Qué mostró Windows |
|---|---|
| Nativo + cable A | nada, ni un dispositivo con error |
| Nativo + cable B | `VID_0000&PID_0002`, error de descriptor |
| CP2104 + cable B | igual: error de descriptor |
| CP2104 + otro puerto PC | nada |
| Otro puerto PC | nada |
| **Nativo otra vez** | **enumera limpio, COM3 vuelve** |

Lecciones:

- **`VID_0000` no es un fabricante.** Es lo que pone Windows cuando no consigue
  leer ni un byte del descriptor. Significa que hay contacto eléctrico pero la
  señal no sirve.
- **"Nada" y "error de descriptor" son cosas distintas.** Nada = el cable no
  lleva datos. Error de descriptor = los lleva pero mal.
- La hipótesis de que se había trabado el periférico USB del ESP32 **era
  equivocada**: si lo estuviera, no habría vuelto solo al reconectar en el mismo
  puerto nativo. Lo consistente con las seis pruebas es que el contacto sea
  marginal.
- Cuidado al medir: si desenchufás para probar, el aparato **se reinicia**, y un
  chequeo hecho antes de que termine de arrancar da un falso negativo. Confirmá
  contra el log del proxy que ya volvió antes de concluir.

Moraleja de fondo: **actualizar este aparato depende de un conector que se porta
así.** Por eso el OTA dejó de ser un lujo — ver Pendiente.

### De los gestos

**Un tope de duración convertía la mitad de los deslizamientos en nada.** El
clasificador tenía cuatro ramas y una de ellas exigía `duration <= 300 ms` para
que un movimiento contara como deslizamiento. Si tardaba más, no era envión (muy
lento), no era pulsación larga (se había movido), no era deslizamiento (tardó
demasiado) y no era toque (se movió demasiado): **caía fuera de las cuatro y
terminaba en `TOUCH_NONE`, en silencio.** Y un deslizamiento hecho con calma
sobre un aparato de escritorio tarda tranquilamente 400 ms. De ahí el "a veces
anda muy bien y a veces no": no dependía del azar, dependía de con cuánta
tranquilidad uno movía el dedo.

Ahora el deslizamiento se reconoce **por distancia, no por duración**, y la
clasificación es exhaustiva: toda soltada produce exactamente un gesto. La regla
es que ningún toque se pierda, porque hacerse el desentendido es lo peor que
puede hacer una pantalla táctil.

**El GT911 saltea reportes.** Cada tanto devuelve cero contactos en medio de un
gesto aunque el dedo siga apoyado. Tomando ese hueco como una soltada, un
deslizamiento se parte en dos mitades, ninguna llega al umbral, y las dos se
descartan. Se exigen **dos lecturas vacías consecutivas** antes de darlo por
terminado.

**El rebote se aplicaba a todo.** Los 120 ms de anti-rebote descartaban
*cualquier* gesto que llegara pegado al anterior, así que dos deslizamientos
seguidos —cambiar de app dos veces rápido— perdían el segundo. Ahora sólo
frenan toques, que es para lo que existían.

**El log lo dice todo, incluso los descartes.** Cada soltada imprime
`[Touch] IZQ dx=-87 dy=12 dur=380ms max=91 vel=-40`. Cuando alguien dice "el
gesto no hizo nada", esa línea contesta si el aparato lo vio y qué midió, en vez
de dejarlo en discusión.

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

**Los acentos andan desde el 2026-08-03, y el mecanismo no es Latin-1.** El
aparato recibe **UTF-8** y LovyanGFX lo decodifica solo (`TextStyle::utf8` viene
en `true`), así que `ñ` le llega como el codepoint `0xF1`. Las fuentes se
regeneraron con rango contiguo `0x20–0xFF`, que es como las indexa
`GFXfont::getGlyph()` cuando `range_num` es 0. No hay conversión de encoding en
ningún lado: el proxy emite UTF-8 y la fuente tiene el glifo.

Para regenerar una fuente hay que sacar el TTF de `tools/fonts/satoshi.zip`
(están en `Satoshi_Complete/Fonts/WEB/fonts/`) y correr:

```powershell
python tools\ttf_to_gfx.py Satoshi-Regular.ttf 6  src\data\Satoshi9.h        Satoshi9        FF
python tools\ttf_to_gfx.py Satoshi-Regular.ttf 9  src\data\Satoshi12.h       Satoshi12       FF
python tools\ttf_to_gfx.py Satoshi-Medium.ttf  12 src\data\SatoshiMedium18.h SatoshiMedium18 FF
```

Los tamaños en puntos no son arbitrarios: salen del encabezado de cada `.h`
generado. Antes de cambiar algo, regenerá con los parámetros actuales y
compará — si el header sale idéntico, identificaste bien el origen. Cuesta
+21,5 KB de flash las tres juntas, sobre 6,5 MB.

LovyanGFX además soporta rangos **no contiguos** (`range_num`/`range` en
`GFXfont`), que ahorrarían los ~455 bytes del tramo muerto `0x7F–0xBF` por
fuente. No se usó: no justifica tocar el formato del generador.

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

**No hay forma de descubrir una Lista pública de X.** Verificado el 2026-08-03,
por los tres caminos: el swagger de Sorsa expone 40 endpoints y ninguno busca
Listas (sólo `/list-tweets`, `/list-members`, `/list-followers`, todos sobre un
ID que ya tengas); `x.com/i/lists/<id>` devuelve 200 hasta con un ID inventado,
pero el cuerpo es el shell de "JavaScript is not available", sin contenido; y los
IDs no están indexados en la web. Si querés una Lista, hay que crearla.

**`/search-tweets` reemplaza a la Lista y es mejor acá.** Acepta la sintaxis de
búsqueda avanzada de X (`from:`, `OR`, frases, hashtags), ordena por `latest` o
`popular`, y devuelve **el mismo `common.TweetsResponse`**. O sea que `parse()`
se reusa tal cual; sólo cambia el request, que es POST con
`{"query": "...", "order": "latest"}` en vez de GET con `list_id`. Se configura
como string en el `.bat`, igual que `RSS_FEEDS`, sin ID que cazar.

**El campo `entities` está sin documentar en el swagger** (figura como `array` a
secas). Su forma real, sacada de una captura del 2026-08-03:

```json
{ "type": "photo", "link": "https://pbs.twimg.com/media/....jpg", "preview": "" }
{ "type": "video", "link": "https://video.twimg.com/....mp4",     "preview": "https://pbs.twimg.com/....jpg" }
```

Regla de extracción: **`preview` si viene, si no `link`.** Hoy `parse()` mapea
`ImgURL` al avatar del autor (`profile_image_url`), no a la media del tweet.

**En X la mayoría de los tweets no traen imagen, y eso importa mucho acá.**
Medido sobre dos consultas de 20 tweets: una selección con Ars Technica dio 4/20
con media (Ars posteó 14 de los 20 y **ninguno** con imagen — son links pelados),
y una de espacio/ciencia dio 8/20. Para un aparato que muestra foto + titular,
conviene **filtrar a los que traen media** en vez de caer al avatar. Ojo también
con el volumen: `order: latest` hace que la cuenta que más postea se coma los
slots. `CERN` y `NASAHubble` no devolvieron nada.

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

**`powershell` remoto por ssh hay que envolverlo en `cmd /c`.** Invocarlo
directo —`ssh ... "powershell -NoProfile -Command ..."`— termina en
`Terminate batch job (Y/N)?` sin imprimir nada, y falla igual con `-n -T` y con
`-EncodedCommand`, o sea que no es un problema de comillas. Envuelto anda:

```powershell
ssh ... "cmd /c powershell -NoProfile -Command Get-Date -Format o"
```

`cmd` solo también funciona (`ssh ... "cmd /c dir"`). Para scripts largos,
mandá un `.bat` con `scp` y corrélo con `cmd /c`: evita el infierno de escapar
comillas a través de ssh.

**Los logs del proxy van a `err.log`, no a `out.log`.** El paquete `log` de Go
escribe a stderr. En `out.log` solo queda lo que imprime el wrapper `.bat`.
Buscar las fuentes levantadas con `findstr fuente C:\ferced\feedproxy\err.log`.

---

## Pendiente

### La transición entre ítems, y por qué ya no parpadea

Cada ítem arrancaba con un `fillScreen` + empuje completo: **99 ms con el panel
en negro**, y recién después los elementos entrando de a uno durante más de un
segundo. Eso era el "las animaciones están medio rotas". No era el framerate —
que estaba medido en 35,5 fps— sino que la pantalla se vaciaba entre ítem e
ítem.

Ahora no se limpia nada por defecto. Cada banda se limpia sola en el frame en
que su elemento empieza a entrar, así el contenido viejo se reemplaza **en ola,
de arriba hacia abajo**, y la pantalla nunca queda vacía. Sale gratis: son las
mismas bandas que ya se repintaban.

Lo que hace falta para que funcione:

- **Las bandas de los renglones se solapan** (cada una llega 22 px más abajo que
  el arranque de la siguiente), así que entre renglón y renglón no quedan
  huecos. Si alguien toca `TEXT_LH` o `UI_RISE_PX`, verificar que siga siendo
  cierto o volverán las franjas sin pintar.
- **Lo que el dibujo nuevo no vaya a tapar hay que borrarlo a mano**: renglones
  que sobran cuando el titular nuevo es más corto, el pie cuando se pasa a una
  pantalla de estado. Lo hace `arrancar()` en `ui_ferced.cpp`.
- **Las pantallas que dibujan directo sobre `tft`** —configuración, selector,
  tareas— dejan el sprite con contenido que ya no está en el panel, así que
  llaman a `uiAnimInvalidate()`. Que lo declare quien ensucia, y no quien viene
  después, hace imposible olvidárselo.

**La imagen del próximo titular se adelanta.** Se bajaba dentro de
`uiFercedShowItem()`, o sea entre el gesto del usuario y el primer píxel que
cambia: 200 ms en los que el aparato parecía no responder. Ahora se adelanta
recién a los 2 s de que la pantalla se quedó quieta —antes caería encima del
segundo toque de quien pasa titulares rápido— y hay dos ranuras de imagen en vez
de una: 8 KB más de RAM a cambio de que la transición no espere a la red.

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

**El contenido de X: implementado, falta desplegarlo.** Era el pedido original.
Ya no depende de que exista una Lista: `sorsa.SearchSource` trae tweets por
consulta contra `/v3/search-tweets`, con extracción de la imagen del tweet desde
`entities` y filtro que descarta los que no traen ninguna. 14 tests, incluido
uno que valida el body del POST — armarlo mal no da error, devuelve vacío.

Falta sólo poner las variables en el `.bat` del VPS y reiniciar el servicio. La
selección medida y recomendada:

```bat
set "SORSA_QUERY=from:NASA OR from:esa OR from:NASAWebb"
```

Por qué esas: se midió la proporción de tweets con imagen, y es lo que decide
si el aparato muestra una foto o el avatar del autor. NASA 5/10, NASAWebb 1/1,
esa 1/2; `NatGeo` sólo 1/6 y `CERN`/`NASAHubble` no devolvieron nada. Ars
Technica quedó afuera por lo mismo: 0 de 14, y por volumen se comía la mayoría
de los slots. Complementa a los RSS —que ya cubren titulares generales y tech—
con imágenes de ciencia, y al ser en inglés esquiva el bug de Latin-1.

`SORSA_LIST_ID` sigue funcionando si alguna vez existe una Lista; las dos
fuentes conviven y el mezclador las intercala por separado.

**Modo Archillect.** Pedido: al tocar el logo, mostrar contenido de
archillect.com hasta que el usuario cierre.

**La API existe y está viva.** Verificado el 2026-08-03, y a diferencia de X:
los endpoints responden `application/json` con `{"error":"Token missing."}`, que
es una API sana pidiendo credenciales, no un cadáver devolviendo cuerpo vacío.
Documentada en `archillect.com/api`.

**Pero está detrás de Patreon**, tier "Contributor o superior", en
`patreon.com/pak`. Sin token no se avanza. Los precios están tras JS y no se
verificaron.

Endpoints documentados (todos GET, `?token=`):

| Endpoint | Devuelve |
|---|---|
| `/last/[count]` | hasta 288 posts recientes, con imagen en varios tamaños |
| `/post/[index]` | un post: index, imágenes multi-tamaño, captions, sources |
| `/tv` | contenido actual + `buffer` (el siguiente), refresca cada 6 s |
| `/date/`, `/keyword/`, `/filter/`, `/relatives/` | búsqueda y relacionados |

Lo que se aprendió del spike, más allá de la API:

- Los índices son **secuenciales** (el último era 410187) y `archillect.com/<id>`
  es la página del post. Direccionamiento simple y estable.
- Las imágenes viven en el **CDN de Tumblr** (`66.media.tumblr.com`) con sufijos
  de tamaño estándar (`_250`, `s250x400`). Como la API expone varios tamaños, el
  proxy puede pedir ~540 y bajar a 480: es exactamente lo que ya hace el
  pipeline de miniaturas.
- **~14 % del contenido son GIF** (5 de 36 en la portada). El `/tv` es GIF por
  definición. Eso es otro problema, no el mismo más grande.
- No hay `robots.txt`: 302 a la home.

**Corrección al cálculo que estaba acá antes.** Decía que los 460 KB por imagen
contra 8 KB de miniatura eran "otro pipeline", como si el problema fuera la
pantalla. No lo es: en PSRAM 460 KB no es nada, y por la ecuación medida de este
mismo documento un push de 480 filas cuesta `23,4 + 0,1575 × 480 = 99 ms`, que
es justo lo que ya paga `beginSlide()` una vez por ítem. Para un pase de
diapositivas cada varios segundos, 99 ms es irrelevante.

**El costo real es la red:** 460 KB por imagen contra los 8 KB de hoy, o sea
segundos por imagen sobre HTTPS. Eso fija la cadencia, no el panel. Se ataca con
prefetch — y no por casualidad el `/tv` te entrega el `buffer` del siguiente.

**Los GIF sí son otro pipeline.** Un 480x480 animado en RGB565 crudo son ~4,6
MB/s a 10 fps: no entra por WiFi. Para v1 conviene stills de `/last/` y GIF
resuelto como primer cuadro (o salteado); `/tv` queda para v2.

**Habilitar el OTA — el más importante de todos.** Hoy la única forma de
actualizar el aparato es por USB, y el 2026-08-04 eso costó seis combinaciones
de cable y puerto (ver trampas del hardware). El WiFi, en cambio, no falló una
sola vez en toda la sesión. Que actualizar dependa del conector más frágil del
aparato es la fragilidad real del proyecto.

Las dos piezas difíciles ya están:

- **La tabla de particiones lo soporta.** `default_16MB.csv` trae `otadata`,
  `app0` (ota_0) y `app1` (ota_1) de 6,5 MB cada una. No hay que reparticionar,
  que era el escenario que mataba la idea porque también habría pedido USB.
- **`src/ota_manager.cpp` ya existe**, con descarga por HTTPS sobre `Update.h`,
  comparación de versiones semánticas con prerelease y progreso en pantalla.
  Está sólo excluido del build en `platformio.ini` (`-<ota_manager.cpp>`).

Lo que falta decidir antes de escribir código: de dónde baja el binario (lo
natural es el proxy, que ya está autenticado con el token), cómo se dispara
(la pantalla de configuración es el lugar obvio para un botón manual) y cómo se
autentica. **Ojo con lo último: un endpoint de OTA es una vía para ejecutar
código arbitrario en el aparato.** No es una decisión para tomar al pasar.

Y el huevo y la gallina que conviene tener presente: **habilitar el OTA requiere
un flasheo por USB.** No sirve para salir de un apuro; sirve para que no haya
un próximo apuro.

**Cerrar la brecha de framerate** es lo otro que queda del lado del firmware.

**Un logo de Ferced en tamaño grande.** El portal cautivo y la pantalla de
provisioning ya son de Ferced —título, paleta, pie, wordmark— pero el único
asset de marca que existe es `data/ferced_mark_11.h`, de **11x28**, y el
`logo_crop.png` del que salió no está versionado. Por eso el portal usa un
wordmark tipográfico en vez de un logo, y la pantalla del aparato dibuja el mark
a escala entera 2x al lado del texto. Agrandar 11x28 con suavizado lo ensucia.
Con un SVG o un PNG grande del isotipo, se regenera con `tools/png_to_rgb565.py`
y se reemplazan las dos cosas.

**El provisioning no está verificado en pantalla.** El aparato tiene
credenciales guardadas y arranca directo a RUNNING, así que para verlo hay que
borrar la NVS y volver a aparearlo con el teléfono. Se verificó extrayendo el
HTML del binario compilado. El simulador tampoco sirve: sólo compila
`ui_ferced.cpp`, y sumar `wifi_provision.cpp` pediría stubear WiFi,
AsyncWebServer y QRCode.

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

**Un raspado de un sitio ajeno que falla en silencio se lee como una app rota.**
El 2026-08-05, horas después de estrenar la app de pádel, dejó de mostrar
partidos. El síntoma no decía nada: pantallas de torneo sin jugadores. La causa
fue un tirón de `widget.matchscorerlive.com` —que normalmente responde en menos
de dos segundos y esa vez se pasó de los 25 del timeout—, y el `if err == nil`
que envolvía la llamada se tragó el error sin registrar una línea. Peor: el
snapshot vacío se cacheaba cinco minutos, así que un tropiezo de un segundo
apagaba la app durante cinco.

Tres arreglos, y el más importante no es el reintento: **registrar siempre el
fallo**, **conservar el último orden de juego bueno** del mismo torneo y el
mismo día (lo mismo que hace el mixer del feed), y **cachear menos tiempo** un
snapshot incompleto para reintentar en un minuto en vez de cinco. Es la misma
moraleja que la del `esp_err_t` de la ISR de VSync: un error que se ignora
porque "igual anda" termina costando caro.

**Un botón que "no hace nada" puede estar haciendo todo.** El 2026-08-05 el
botón «Actualizar feed» parecía roto. El log del proxy mostraba que el pedido
salía y se respondía con 20 ítems: lo que fallaba era el efecto, no la acción.
El mixer tiene un TTL de 10 minutos, así que dentro de esa ventana devolvía
exactamente el mismo pool, y el firmware volvía siempre al índice 0 — o sea, al
mismo titular que ya estaba en pantalla. Encima el GET bloquea el loop, así que
la pantalla de configuración se quedaba congelada sin decir nada.

Tres arreglos, y ninguno era "arreglar el botón": `fresh=1` para saltear el TTL
(con piso de 20 s), la franja de estado contando qué pasó, y quedarse en la
pantalla en vez de salir corriendo. **Antes de buscar por qué algo no anda,
verificá si de verdad no anda**: acá el mecanismo estaba perfecto y lo roto era
el resultado observable.

**Verificá que el instrumento distinga lo que decís que distingue.** El 2026-08-05
se intentó diagnosticar el botón del OTA contando pedidos en el log del proxy,
sin notar que esa línea no anota el user agent y que el aparato y la PC comparten
IP pública. La mitad de los pedidos contados eran propios. Antes de sacar una
conclusión de un log, preguntate si el log puede sostenerla.

Lo mismo con la captura por serie de esa sesión: se perdió una línea que el
firmware imprime de forma incondicional, y por un rato pareció que el firmware
no la había impreso. Cuando el dato falta, la primera sospecha va sobre el
instrumento, no sobre el aparato.

**Y desconfiá del instrumento también.** Al arreglar la ISR de VSync el
instrumento marcó 45 fps sobre un panel de 42: imposible, y la pista de que
medía mal. Medía sólo el costo dentro del tick. El número real era 35,5. Un
resultado que **supera** el techo teórico es tan sospechoso como uno que no
llega — en los dos casos, andá a buscar el dato crudo.

---

## Archivos que importan

| Ruta | Qué es |
|---|---|
| `src/ui_anim.cpp` | Motor compartido: sprite, bandas, VSync, telemetría |
| `src/ui_ferced.cpp` | La app de noticias |
| `src/ui_padel.cpp` | La app de pádel |
| `src/ui_todo.cpp` | La app de tareas |
| `src/todo_store.cpp` | La lista y su persistencia en NVS |
| `src/web_server.cpp` | El servidor del aparato: editor de tareas y entrada de avisos |
| `src/notif_store.cpp` | Los avisos en memoria |
| `src/ui_notif.cpp` | La tarjeta que interrumpe y la lista |
| `src/ui_launcher.cpp` | El selector de apps |
| `src/ferced_main.cpp` | Máquina de estados: provisioning, WiFi, apps, gestos |
| `src/feed_client.cpp` | Cliente HTTP del feed y las imágenes |
| `src/padel_client.cpp` | Cliente HTTP de `/v1/padel` |
| `proxy/internal/padel/` | Raspado del calendario FIP y del orden de juego |
| `proxy/cmd/padelcheck/` | Prueba a mano contra los sitios reales, y el fixture del simulador |
| `proxy/internal/` | Fuentes, mezclador, normalizador, imágenes, guard |
| `sim/shot.ps1` | Compilar + correr + capturar, un comando |
| `docs/plans/2026-08-02-ferced-display-design.md` | Diseño validado |
| `docs/plans/2026-08-02-ferced-display.md` | Plan de implementación |
| `output/release-beta74-*/ROLLBACK.md` | Cómo volver al firmware de fábrica |

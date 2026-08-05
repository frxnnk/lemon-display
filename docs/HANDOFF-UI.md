# ferced-display — traspaso de UI

Estado al 2026-08-05, firmware 1.3.0. Rama `feat/ferced-display`.

Este documento es para quien venga a **mejorar la interfaz**. El traspaso
general está en `docs/HANDOFF.md` y cubre el proxy, las fuentes de datos, el
OTA y la operación del VPS: leelo si vas a tocar de dónde salen los datos. Acá
está todo lo que hace falta para dibujar.

Leelo entero antes de mover un píxel. **La mitad de las decisiones de esta UI no
son estéticas: son consecuencia de un panel lento**, y no se deducen del código.

---

## 1. Lo primero: el presupuesto de frame

Todo lo que sigue existe por estos tres números, que están **medidos en el
aparato**, no estimados.

| Dato | Valor | De dónde sale |
|---|---|---|
| Refresco del panel | **42,3 Hz → 23,64 ms por VSync** | `freq_write` 12 MHz sobre 548x518 con porches. Confirmado contando ticks al arrancar |
| Costo de empujar | **`ms = 23,4 + 0,1575 × filas`** | Regresión sobre empujes reales |
| Frame a pantalla completa | **~99 ms** | 23,4 + 0,1575 × 480 |

La consecuencia es dura y hay que tenerla presente todo el tiempo:

> **Una animación no puede repintar la pantalla entera.** A pantalla completa el
> techo son 10 fps. Animar exige tocar una franja de ~60–70 px por frame, que
> cuesta ~33 ms y entra en un VSync y medio.

Framerate real medido de la animación de noticias: **35,5 fps** (costo 22,7 ms
dentro del tick, período de reloj de pared 28,0 ms) sobre un techo de panel de
42. El número que importa es el **período**, no el costo: el costo deja afuera
el `delay()` del loop, el sondeo del táctil y `wifiLoop()`.

Si tocás la animación, medí. La telemetría sale sola por serie al terminar cada
transición:

```
[anim] frames=45 cost=22.7ms max=35.4ms period=28.0ms fps=35.7 waits=48 timeouts=0
```

`timeouts` es el dato que delata si `displayWaitVSync()` está sincronizando de
verdad o agotando su timeout. Si `timeouts` ≈ `waits`, la ISR de VSync no está
registrada y perdiste un tercio del framerate sin que nada avise.

---

## 2. Las dos familias de pantalla

**Esta es la distinción más importante del código de UI.** Hay dos formas de
dibujar y usar la equivocada rompe cosas.

### A. Animadas — sobre el sprite, por bandas

`ui_ferced.cpp` (noticias) y `ui_padel.cpp` (pádel).

Componen sobre `uiSprite`, un `LGFX_Sprite` de 480x480 en PSRAM, y empujan
**sólo la franja viva** de cada frame. Los elementos entran escalonados: cada
uno tiene un *slot*, y el slot `n` empieza a entrar a los `n × UI_STAGGER_MS`.

```
UI_ENTER_MS   = 130   // lo que tarda un elemento en entrar
UI_STAGGER_MS = 140   // separación entre elementos
UI_RISE_PX    = 22    // cuánto sube al entrar
```

**`STAGGER > ENTER` es a propósito y no se toca a la ligera.** Con
solapamiento hay dos elementos vivos a la vez, la banda sucia es la unión de los
dos (~180 px) y el frame cae en 2 VSync. Sin solapamiento la banda es un
elemento (~70 px) y entra en uno solo. Ya se probó lo contrario y empeoró.

### B. Estáticas — directo sobre `tft`, un solo dibujado

`ui_config.cpp`, `ui_launcher.cpp`, `ui_todo.cpp`, `ui_notif.cpp`.

Se dibujan enteras de una vez (99 ms) y no se animan. **No es pereza**: son
pantallas cuyos elementos ocupan casi todo el alto, así que la banda sucia
terminaría siendo el panel entero y una entrada escalonada correría a 10 fps. Y
además un menú, una lista o un aviso no se miran entrar: se usan.

99 ms aparecen como un cambio de pantalla instantáneo. No hay nada que arreglar
ahí.

### El contrato entre las dos: `uiAnimInvalidate()`

Las estáticas dibujan sobre `tft` y **dejan el sprite con contenido que ya no
está en el panel**. Si después una animada empuja una banda, esa banda sale del
sprite viejo y aparece contenido fantasma.

Por eso cada pantalla estática llama a `uiAnimInvalidate()` al empezar a
dibujar. **Que lo declare quien ensucia, y no quien viene después, es lo que
hace imposible olvidárselo.** Si agregás una pantalla estática nueva, copiá esa
línea.

`uiAnimBegin()` devuelve `true` si limpió toda la pantalla (porque estaba
invalidada) y `false` si no. Ver el punto 3.

---

## 3. La transición en ola (y por qué no hay que romperla)

Hasta la 1.2.1 cada ítem arrancaba con `fillScreen` + empuje completo: **99 ms
con el panel en negro**, y recién después los elementos entrando de a uno
durante más de un segundo. Eso se veía como "las animaciones están medio rotas".
No era el framerate: era que la pantalla se vaciaba entre ítem e ítem.

Hoy **no se limpia nada por defecto**. Cada banda se limpia sola en el frame en
que su elemento empieza a entrar, así el contenido viejo se reemplaza en ola de
arriba hacia abajo y **la pantalla nunca queda vacía**.

Lo que hace falta para que siga funcionando:

1. **Las bandas de renglones consecutivos tienen que solaparse.** Con
   `TEXT_LH = 38` y `UI_RISE_PX = 22`, la banda del renglón *i* llega hasta
   `TEXT_Y + 38i + 60` y la del *i+1* arranca en `TEXT_Y + 38i + 34`. Se
   superponen 26 px. **Si cambiás `TEXT_LH` o `UI_RISE_PX`, verificá que siga
   siendo cierto** o vuelven las franjas sin pintar entre renglones.
2. **Lo que el dibujo nuevo no vaya a tapar hay que borrarlo a mano.** Lo hace
   `arrancar()` en `ui_ferced.cpp`: renglones que sobran cuando el titular nuevo
   es más corto que el anterior, y el pie cuando se pasa a una pantalla de
   estado.
3. **Un cambio de geometría invalida.** `ui_padel.cpp` llama a
   `uiAnimInvalidate()` cuando cambia de modo (partido ↔ torneo) porque los
   huecos de un modo caen sobre el contenido del otro.

**Cómo verificarlo** (ver punto 6): congelar la transición a mitad de camino y
mirar la captura. Si en algún momento la pantalla queda vacía, se rompió.

---

## 4. La identidad

Paleta, en RGB565. Vive en `src/ui_anim.h`, namespace `FercedColors`.

| Nombre | Valor | Qué es |
|---|---|---|
| `CANVAS` | `#0e1011` | el fondo, siempre |
| `SURFACE` | blanco 4% | pastillas, chips |
| `FG` | `#ffffff` | lo que importa |
| `FG_2` | blanco 75% | segundo nivel |
| `FG_3` | blanco 55% | apoyo |
| `FG_4` | blanco 40% | al borde de lo legible |
| `LINE` | blanco 10% | reglas y bordes |
| `SUCCESS` | `#34d399` | **sólo estado** |
| `DANGER` | `#f87171` | **sólo estado** |

**Los grises no son grises inventados: son blanco con alpha sobre el canvas.**
Si necesitás un tono nuevo, derivalo igual, no lo elijas a ojo.

**Regla dura: el color de acento está reservado para estado.** «En juego» va en
`SUCCESS`, «reaparear WiFi» en `DANGER`. Para todo lo demás —qué app está
activa, qué tarea está hecha, quién ganó un partido— **se usa contraste, no
color**. El selector dibuja la app activa en negativo; la casilla marcada se
rellena; el perdedor de un partido baja a `FG_3`. Eso es deliberado y es lo que
hace que la pantalla se vea de una pieza.

**La marca.** `data/ferced_mark_11.h`, 11x28 px, arriba a la derecha en casi
todas las pantallas. Se dibuja a escala entera; agrandarla con suavizado la
ensucia. **No existe un asset más grande**, y es una limitación real: por eso el
portal cautivo usa un wordmark tipográfico en vez del logo. Si conseguís un SVG
o un PNG grande del isotipo, se regenera con `tools/png_to_rgb565.py`.

---

## 5. Tipografía — leé esto antes de escribir un `drawString`

Las fuentes son bitmaps generados de Satoshi. **Cada una tiene un rango de
caracteres y salirse del rango dibuja basura**, sin ningún error.

| Fuente | Tamaño | Rango | Accesor |
|---|---|---|---|
| `Satoshi9` | 6pt | **0x20–0xFF** | `DS::fontCaption()` |
| `Satoshi12` | 9pt | **0x20–0xFF** | `DS::fontBody()` |
| `SatoshiMedium18` | 12pt | **0x20–0xFF** | `DS::fontHeading()`, `fontButton()` |
| `SatoshiBold24` | 16pt | **0x20–0xFF** | `DS::fontDataLg()`, `fontDisplay()` |
| `SatoshiBold40` | 24pt | **0x20–0x7E** ⚠ | `DS::fontHero()` |
| `PPNeueMachinaBold24` | 24pt | **0x20–0x7E** ⚠ | (heredada de Lemon) |

**Las cuatro primeras tienen castellano completo.** `SatoshiBold40` **no**: si
escribís «día» o «PRÓXIMO» con ella, la vocal acentuada sale rota. Ya pasó dos
veces:

- «día 4 de 8» con `fontDataLg()` salía `d|a` porque esa fuente todavía estaba
  en 0x7E. Se regeneró.
- El chip usaba `fonts::Font0`, que es ASCII, y el punto medio `·` (U+00B7)
  salía como un glifo roto. Ahora los chips usan `DS::fontCaption()`.

**Cómo regenerar una fuente.** Sacar el TTF de `tools/fonts/satoshi.zip`
(están en `Satoshi_Complete/Fonts/WEB/fonts/`) y correr:

```powershell
python tools\ttf_to_gfx.py Satoshi-Bold.ttf 24 src\data\SatoshiBold40.h SatoshiBold40 FF
```

El tamaño en puntos **no es arbitrario**: sale del encabezado del `.h` actual.
**Antes de cambiar nada, regenerá con los parámetros actuales y compará**: si el
header sale idéntico (salvo el fin de línea, que el repo tiene en CRLF),
identificaste bien el origen. Cuesta ~5 KB de flash por fuente extender a 0xFF.

**El encoding no es Latin-1.** El proxy emite UTF-8, LovyanGFX lo decodifica
solo (`TextStyle::utf8` viene en `true`) y `ñ` llega como el codepoint `0xF1`,
que cae dentro del rango contiguo de la fuente. No hay conversión en ningún
lado: no la agregues.

**El texto llega ya recortado y en castellano desde el proxy.** Fechas, meses,
fases de un torneo, horas en 24 h: todo eso se resuelve del otro lado. El
firmware no hace aritmética de calendario ni traduce. Si necesitás un texto
nuevo con formato, el lugar correcto casi siempre es el proxy.

---

## 6. El simulador — tu herramienta principal

Corre **el mismo `ui_*.cpp` que el firmware** con la misma LovyanGFX sobre SDL,
y reproduce el costo de frame medido en el aparato. El ciclo por hardware es de
minutos; el del simulador, de segundos.

```powershell
cd sim
python tools\fetch_fixture.py     # contenido real del proxy, una sola vez

.\shot.ps1 -Advance 1             # noticias, ítem 1 (los impares tienen imagen)
.\shot.ps1 -Padel -Advance 1      # pádel: 0 es el torneo, 1.. los partidos
.\shot.ps1 -Tareas                # la lista de tareas
.\shot.ps1 -Avisos                # la lista de avisos
.\shot.ps1 -Aviso                 # la tarjeta que interrumpe
.\shot.ps1 -Launcher              # el selector de apps
.\shot.ps1 -Config                # configuración
.\shot.ps1 -Progreso 45           # configuración con la descarga del OTA al 45%
.\shot.ps1 -NoBuild -Out x.png    # sin recompilar, a otro archivo
```

**Para mirar una transición por dentro**, que es donde vive lo que hay que
verificar:

```powershell
.\shot.ps1 -Advance 3 -Hacia 1 -Congelar 420
```

Muestra el ítem 3, lo deja terminar de entrar, pasa al 1 y **congela el dibujo a
los 420 ms**. Lo que tiene que verse: el titular nuevo entrando arriba **y el
pie del anterior todavía en pantalla**. Si la pantalla queda vacía, volvió el
parpadeo.

Y de un ítem largo a uno corto con `-Congelar 2000` verifica que no queden
renglones huérfanos colgados.

### **Mirá siempre la captura**

Seis bugs serios se encontraron a simple vista y habrían costado horas de
flasheos a ciegas: franjas de fondo sin pintar, miniaturas con los colores
rotos, la «í» partida, el punto medio roto, el selector desbordado con tres
tarjetas y el texto cruzando el borde con cuatro. **Ninguno daba error de
compilación.**

### Los fixtures

| App | Archivo | Cómo se regenera |
|---|---|---|
| Noticias | `sim/data/fixture.txt` + `.bin` | `python tools\fetch_fixture.py` |
| Pádel | `sim/data/padel.txt` | `cd proxy && go run ./cmd/padelcheck -fixture ../sim/data/padel.txt` |
| Tareas | `sim/data/todo.txt` | a mano |
| Avisos | `sim/data/avisos.txt` | a mano |

**Que los datos sean reales importa.** La pantalla de pádel se rompe justamente
con los apellidos largos («Santiago Jose Pineda Cabello»), y un fixture
inventado los escondería. Los fixtures a mano ya traen a propósito un caso
largo que fuerza el recorte con puntos suspensivos.

### La regla que no hay que romper

**El simulador usa el archivo real, no una copia.** Un `#ifdef FERCED_SIM` en
`display_manager.h` en vez de duplicar `ui_ferced.cpp`. Si alguna vez tenés que
duplicar un archivo para que el simulador compile, **frená**: una copia se
desincroniza y el simulador empieza a mentir.

Si agregás un `ui_*.cpp`, sumalo a `sim/build.ps1` en `$sources`. Si toca un
cliente (HTTP, NVS), hace falta un `sim/src/sim_*.cpp` que implemente esa API
desde un archivo.

---

## 7. Las pantallas, una por una

480x480. Margen estándar **28 px** en todo salvo configuración, que usa 32.

### Noticias — `ui_ferced.cpp` (animada, 8 slots)

```
 30  chip de fuente (pastilla SURFACE, Satoshi9 FG_3)        marca 11x28 →
118  titular, hasta 5 renglones, SatoshiMedium18 FG, LH 38
356  pie: miniatura 64x64 + autor (Satoshi12 FG_2) + tiempo (Satoshi9 FG_3)
452  línea de progreso hacia el próximo ítem
```

Slots: 0 = chip y marca, 1–5 = renglones, 6 = pie, 7 = progreso.
El texto se corta **por ancho real en píxeles** (`wrapText`), no contando
caracteres: con fuente proporcional contar caracteres deja renglones desparejos.

En reposo repinta 4 veces por segundo sólo la línea de progreso, para que avance
suave sin costo perceptible.

### Pádel — `ui_padel.cpp` (animada, 7 slots, dos modos)

Modo partido: `98` cuándo (fecha · hora) → `124` qué (fase · género) →
`172/208` pareja A → `250` regla corta → `282/318` pareja B → `396` pie
(cancha · día del torneo) → `452` progreso.

La jerarquía responde a preguntas: **cuándo arriba, qué en el medio, dónde al
pie**. Las dos parejas son lo único en tipografía de título. El resultado va a
la derecha; el perdedor de un partido terminado baja a `FG_3`, que es la forma
más barata de que se lea quién ganó.

Modo torneo: nombre en hasta dos renglones, la cuenta regresiva en
`fontDataLg()`, rango de fechas y sede en Satoshi12.

Las filas de cada modo se cargan en `s_rows[]` al preparar la pantalla, así el
cálculo de banda sucia no tiene que saber en qué modo está.

### Selector de apps — `ui_launcher.cpp` (estática)

Encabezado con marca + wordmark «FERCED» (la única pantalla donde el aparato
habla de sí mismo), regla en `100`, y una tarjeta por app.

**La grilla se calcula a partir de cuántas apps hay.** Estuvo clavada para dos y
al sumar la tercera se salía del panel; con la cuarta el bloque de texto se
cruzaba con el borde de abajo. Hoy: `CARD_Y0 120`, `CARD_FIN 408`, `GAP 16`,
altura = lo que quede repartido, tope 126. El aire entre nombre y estado se
aprieta (6 → 2 px) cuando la tarjeta baja de 90 px, y los altos de texto
(`NOMBRE_H 26`, `ESTADO_H 20`) están **medidos, no supuestos**.

`MAX_APPS = 4`. **Si agregás una quinta app, esto hay que rehacerlo** —
probablemente en dos columnas.

### Tareas — `ui_todo.cpp` (estática)

Chip con las pendientes, 7 filas de 46 px desde `96`, casilla de 22 px a la
izquierda, texto en `fontHeading()` recortado con puntos suspensivos. Hechas:
casilla rellena con un tilde dibujado a mano (las fuentes no tienen el glifo) y
texto tachado con una `drawFastHLine` del ancho real del texto.

Al pie, la dirección para editarla desde el teléfono. **Se le pasa armada desde
afuera** (`uiTodoDraw(direccion)`) porque esta pantalla no consulta WiFi: así
también compila en el simulador.

### Avisos — `ui_notif.cpp` (estática, dos vistas)

Tarjeta que interrumpe: chip con la fuente, título en `fontDataLg()` hasta dos
renglones, cuerpo en Satoshi12 hasta tres, «hace tanto» al pie y una barra de
4 px que se agota hasta el cierre automático (25 s). La barra se repinta sola
cuatro veces por segundo: es una franja de 4 px, no la pantalla.

Lista: 6 filas de 60 px, punto lleno = sin leer, hueco = leído.

### Configuración — `ui_config.cpp` (estática)

Margen 32. Tres bloques de datos separados por reglas centradas en el aire que
sigue —no pegadas al bloque de arriba, para que separen en vez de parecer que
subrayan—, franja de estado en `322` y cuatro botones de 44 px en dos filas.

**Los anchos de los botones están medidos**, no elegidos: con SatoshiMedium18
los rótulos miden 162 («Actualizar feed»), 217 («Buscar actualización»), 69
(«Cerrar») y 110 («Reaparear»). La columna ancha de 248 le deja 15 px de aire al
más largo. «Reaparear WiFi» medía 163 y se desbordaba de la pastilla angosta: el
rótulo perdió el sustantivo, no el verbo.

La franja de estado repinta **sólo su rectángulo**. Es lo que permite mostrar el
avance de una descarga, que informa hasta 101 veces: con `fillScreen` serían
diez segundos de dibujo.

---

## 8. Gestos

`src/touch_manager.cpp`. GT911 vía LovyanGFX.

| Gesto | Qué hace |
|---|---|
| Toque | siguiente ítem; en tareas marca la que se tocó; en avisos, nada |
| Deslizar ← o → | cambiar de app |
| Deslizar ↑ | abrir el selector |
| Deslizar ↓ | en el selector, cerrarlo |
| Pulsación larga | configuración |

Umbrales: `SWIPE_MIN_PX 45`, `TAP_SLOP_PX 24`, `LONG_PRESS_MS 500`,
`GESTO_MAX_MS 2500`.

**Tres reglas que costaron caro y no conviene revertir:**

1. **El deslizamiento se reconoce por distancia, no por duración.** Había un
   tope de 300 ms y todo lo que tardaba más caía fuera de las cuatro ramas del
   clasificador y terminaba en `TOUCH_NONE`, en silencio. Un deslizamiento hecho
   con calma tarda 400 ms tranquilamente. Era la causa de "a veces anda y a
   veces no".
2. **La clasificación es exhaustiva: toda soltada produce exactamente un
   gesto.** Hacerse el desentendido es lo peor que puede hacer una pantalla
   táctil.
3. **El GT911 saltea reportes**: devuelve cero contactos en medio de un gesto
   aunque el dedo siga apoyado. Se exigen **dos lecturas vacías consecutivas**
   antes de dar la soltada por buena, o un deslizamiento se parte en dos mitades
   que no llegan al umbral.

Cada soltada se registra por serie con sus medidas, descartes incluidos:

```
[Touch] IZQ dx=-87 dy=12 dur=380ms max=91 vel=-40
```

Cuando alguien diga «el gesto no hizo nada», esa línea contesta si el aparato lo
vio y qué midió. Usala antes de tocar umbrales.

---

## 9. Qué está flojo — candidatos reales de mejora

Honestamente, y en orden de cuánto se nota:

1. **No hay transición entre apps.** Deslizás y la pantalla nueva aparece de
   golpe. Un deslizamiento horizontal pide un movimiento horizontal, pero eso
   cuesta 99 ms por frame y sería peor que nada. **La única dirección barata es
   la vertical** (el costo es proporcional a las filas empujadas): una cortina
   que revele de arriba hacia abajo en bandas sí entra en presupuesto. Vale la
   pena explorarlo — sería el cambio más visible de todos.
2. **El selector no se anima.** Está justificado con las tarjetas actuales, pero
   si se rediseñaran más bajas la banda sucia bajaría y la entrada escalonada
   volvería a entrar en presupuesto.
3. **La marca sólo existe en 11x28.** Techo real para cualquier pantalla que
   quiera presencia de marca. Ver punto 4.
4. **`MAX_APPS = 4` y ya está lleno.** Una quinta app obliga a repensar el
   selector.
5. **Queda framerate sobre la mesa.** El período es 28,0 ms y el del panel 23,6:
   ~18% de los frames se pasan del presupuesto y esperan al flanco siguiente.
   Recortar un par de ms del trabajo por vuelta metería esos frames en un solo
   período y empujaría hacia los 42 fps del techo. `freq_write` a 16 MHz daría
   56 Hz de panel pero aumenta la contención de PSRAM. **Sin medir, no.**
6. **La pantalla de aprovisionamiento nunca se verificó en el panel.** El
   aparato tiene credenciales guardadas y arranca directo. Para verla hay que
   borrar la NVS. El simulador tampoco sirve: pediría stubear WiFi,
   AsyncWebServer y QRCode.
7. **La página web de tareas** (`src/web_server.cpp`, en PROGMEM) es la otra
   superficie de UI y está mucho menos trabajada que la pantalla. Se itera
   con `scratchpad/extraer_pagina.py`, que la saca del `.cpp` y le enchufa un
   simulacro de la API para abrirla en el navegador.

---

## 10. Reglas que no conviene revertir

- **Medí antes de optimizar.** El paso «slots en secuencia» empeoró el
  framerate y sólo se supo por la telemetría.
- **Y desconfiá del instrumento.** Al arreglar la ISR de VSync el instrumento
  marcó 45 fps sobre un panel de 42: imposible, y ésa fue la pista de que medía
  mal. Un resultado que **supera** el techo teórico es tan sospechoso como uno
  que no llega.
- **El firmware no conoce las fuentes de datos.** Si te ves agregando lógica de
  formato, fechas o idioma en un `ui_*.cpp`, casi seguro va en el proxy.
- **El proxy resuelve las imágenes.** Baja, recorta a cuadrado, escala y
  convierte a RGB565. El ESP32 sólo pinta píxeles.
- **`pushImage` con un `uint16_t*` pelado asume orden intercambiado** y la
  imagen sale con los colores rotos. Hay que castear a
  `(const lgfx::rgb565_t*)`.
- **LovyanGFX no tiene `drawSmoothRoundRect`.** Un borde de 1 px se arma con dos
  rellenos concéntricos: el de afuera del color de la línea, el de adentro del
  canvas.
- **Compilá con el árbol limpio** antes de publicar por OTA, o el sello de
  versión sale `-dirty` y el aparato muestra un commit que no existe. Ojo con el
  orden: `proxy/feedproxy-vps.exe` está versionado, así que si lo reconstruís
  para desplegar *después* de commitear el firmware, el build sale sucio igual.

---

## 11. Publicar lo que hiciste

El OTA funciona y ya corrió varias veces. Cambiar la UI **no requiere USB**.

```powershell
# 1. subir VERSION en tools/inject_version.py y COMMITEAR
python -m platformio run -e ferced_display_vps
# 2. verificar que el sello no salga sucio
python -c "import re;s=open(r'.pio\build\ferced_display_vps\firmware.bin','rb').read();print('dirty' if re.search(rb'[0-9a-f]{7}-dirty',s) else 'limpio')"
# 3. el binario PRIMERO, la version despues
scp -i $env:USERPROFILE\.ssh\id_ed25519_franco_vps .pio\build\ferced_display_vps\firmware.bin Administrator@173.212.246.68:C:/ferced/firmware/firmware.bin
# 4. version.txt con el mismo numero
```

El aparato lo baja desde la pantalla de configuración → «Buscar actualización».
No chequea solo.

**El rollback nunca se probó.** Si publicás algo que arranca pero se queda sin
red, la red de seguridad es una suposición. Ver `docs/HANDOFF.md`.

Uso actual: **19,8% de flash, 24,6% de RAM**. Hay lugar de sobra.

---

## 12. Mapa de archivos

| Ruta | Qué es |
|---|---|
| `src/ui_anim.cpp/.h` | Motor compartido: sprite, bandas, VSync, telemetría, paleta |
| `src/ui_ferced.cpp` | App de noticias (animada) |
| `src/ui_padel.cpp` | App de pádel (animada) |
| `src/ui_todo.cpp` | App de tareas (estática) |
| `src/ui_notif.cpp` | Avisos: tarjeta que interrumpe + lista (estática) |
| `src/ui_launcher.cpp` | Selector de apps (estática) |
| `src/ui_config.cpp` | Configuración y franja de estado del OTA (estática) |
| `src/design_system.h` | Accesores de fuente, escala de espaciado, radios, altos de botón |
| `src/data/Satoshi*.h` | Las fuentes, generadas |
| `src/data/ferced_mark_11.h` | La marca, 11x28 |
| `src/ferced_main.cpp` | Máquina de fases, gestos, qué app corre |
| `src/touch_manager.cpp` | Clasificación de gestos |
| `src/web_server.cpp` | La página de tareas (en PROGMEM) y la entrada de avisos |
| `sim/build.ps1` | Compila el simulador; acá se agregan los `ui_*.cpp` nuevos |
| `sim/shot.ps1` | Compilar + correr + capturar, un comando |
| `docs/HANDOFF.md` | El traspaso general: proxy, datos, OTA, VPS |

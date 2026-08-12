# ferced-display — traspaso de UI

Estado al 2026-08-05, firmware 1.4.0. Rama `feat/ferced-display`.

> **Qué cambió en la 1.4.0.** Rediseño completo de la interfaz para que el
> aparato se lea como una extensión de ferced.com: escala tipográfica nueva
> (titulares de 12 a 19 pt), la cursiva con gracias del sitio, el espectro del
> isotipo como riel al pie de todas las pantallas, el isotipo a color, un chasis
> común, la cortina entre apps y el selector en grilla de dos columnas.
> Estructuralmente: **todas las pantallas componen sobre el sprite** y **las
> bandas del escalonado teselan las 480 filas**, lo que eliminó el contrato de
> `uiAnimInvalidate()` y la contabilidad de renglones huérfanos. Ver los
> puntos 2 y 3, que son los que cambiaron de raíz.

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

Framerate real medido de la animación de noticias, **1.4.0, ocho transiciones en
el aparato**: costo 22,6 ms (21,8–23,6), período 28,5 ms (27,0–30,8), **35,2 fps**
(32,5–37,0), peor frame 39,7 ms, `timeouts=0` en las ocho. Contra la 1.3.0
—22,7 / 28,0 / 35,7 / 35,4— **no hay regresión**: el período subió 0,5 ms, un 2 %,
dentro de la dispersión.

El número que importa es el **período**, no el costo: el costo deja afuera el
`delay()` del loop, el sondeo del táctil y `wifiLoop()`.

> **Cuidado con el modelo.** Antes de medir, la 1.4.0 predecía un peor frame de
> 43,2 ms sumando `23,4 + 0,1575 × filas` sobre las bandas nuevas. El real fue
> 39,7. El error es de bulto y conviene no repetirlo: **la constante 23,4 de esa
> fórmula incluye la espera de VSync y el contador `cost` no**, así que son
> magnitudes distintas y no se suman. La fórmula sirve para comparar bandas
> entre sí, no para predecir el `cost` que va a reportar la telemetría.

El `max` viene en dos grupos claros, ~23,5 ms y ~36–40 ms: son los ítems **sin** y
**con** miniatura. La banda de la cabecera son 126 filas y el `pushImage` de
64x64 es lo que la encarece; los que no traen imagen dibujan un cuadrado y una
letra y salen a mitad de precio.

**La cortina no está medida todavía**: se instrumentó (`[cortina]` por serie)
pero sólo se dispara con un gesto, así que hace falta alguien delante del
aparato deslizando.

Si tocás la animación, medí. La telemetría sale sola por serie al terminar cada
transición:

```
[anim] frames=45 cost=22.7ms max=35.4ms period=28.0ms fps=35.7 waits=48 timeouts=0
```

`timeouts` es el dato que delata si `displayWaitVSync()` está sincronizando de
verdad o agotando su timeout. Si `timeouts` ≈ `waits`, la ISR de VSync no está
registrada y perdiste un tercio del framerate sin que nada avise.

---

## 2. Todas las pantallas componen sobre el sprite

**Esta es la regla más importante del código de UI, y cambió en la 1.4.0.**

Hasta la 1.3.0 había dos familias: las animadas componían sobre `uiSprite` y
las estáticas dibujaban directo sobre `tft`. Eso dejaba el sprite con contenido
que ya no estaba en el panel, y por eso existía el contrato de
`uiAnimInvalidate()`: cada estática tenía que declarar que había ensuciado.

Hoy **nadie dibuja directo sobre el panel**. Todas componen sobre el sprite y
todo lo que llega al panel es un empuje de una banda del sprite. Lo que hay en
el sprite es lo que hay en el panel, siempre, y el problema de los fantasmas
dejó de existir en vez de estar administrado.

Queda **un solo llamador de `uiAnimInvalidate()`**: `wifi_provision.cpp`, la
pantalla del QR, que sigue dibujando sobre `tft` porque el simulador no la puede
correr —pediría stubear WiFi, AsyncWebServer y QRCode—. Si agregás otra pantalla
que dibuje sobre `tft`, copiá esa línea. Si componés sobre el sprite, no hace
falta nada.

Lo que sí sigue habiendo son **dos formas de mostrarse**:

### A. Con ola — los elementos entran escalonados

`ui_ferced.cpp` (noticias) y `ui_padel.cpp` (pádel).

Empujan **sólo la franja viva** de cada frame. Cada elemento tiene un *slot*, y
el slot `n` empieza a entrar a los `n × UI_STAGGER_MS`.

```
UI_ENTER_MS   = 130   // lo que tarda un elemento en entrar
UI_STAGGER_MS = 140   // separación entre elementos
UI_RISE_PX    = 22    // cuánto sube al entrar
```

**`STAGGER > ENTER` es a propósito y no se toca a la ligera.** Con
solapamiento hay dos elementos vivos a la vez, la banda sucia es la unión de los
dos (~180 px) y el frame cae en 2 VSync. Sin solapamiento la banda es un
elemento y entra en uno solo. Ya se probó lo contrario y empeoró.

### B. Con cortina — `uiAnimReveal()`

`ui_config.cpp`, `ui_launcher.cpp`, `ui_todo.cpp`, `ui_notif.cpp`.

Componen la pantalla entera sobre el sprite y terminan en `uiAnimReveal()`. Sus
elementos ocupan casi todo el alto, así que una entrada escalonada tendría la
banda sucia del panel entero y correría a 10 fps. Y además un menú, una lista o
un aviso no se miran entrar: se usan.

`uiAnimReveal()` empuja de una sola vez —99 ms, que se leen como instantáneo—
**salvo que alguien haya pedido la cortina** con `uiAnimCurtainOnce()`. Con
cortina el revelado baja en ocho bandas de 60 px con un filo de 2 px del
espectro de la marca barriendo hacia abajo: cuesta lo mismo en total pero
repartido en frames que entran en el presupuesto, así que el táctil se sigue
atendiendo mientras pasa.

**Quién pide la cortina:** `enterApp()` y `enterLauncher()` en
`ferced_main.cpp`, o sea el cambio de pantalla completa. Un repintado dentro de
la misma pantalla —marcar una tarea, mover la barra de un aviso— no la pide y
sale instantáneo, que es lo correcto para responder a un toque. La bandera vive
en `ui_anim.cpp` y no en cada pantalla: las pantallas no tienen por qué
enterarse de por qué las están dibujando.

`uiAnimBegin()` consume el pedido sin usarlo, porque una pantalla animada ya
entra bajando de arriba hacia abajo con su propia ola y sería el mismo gesto dos
veces.

---

## 3. La ola, las bandas que teselan, y por qué no hay que romperlo

Hasta la 1.2.1 cada ítem arrancaba con `fillScreen` + empuje completo: **99 ms
con el panel en negro**, y recién después los elementos entrando de a uno
durante más de un segundo. Eso se veía como "las animaciones están medio rotas".
No era el framerate: era que la pantalla se vaciaba entre ítem e ítem.

Hoy **no se limpia nada, nunca**. Cada banda se limpia sola en el frame en que
su elemento empieza a entrar, así el contenido viejo se reemplaza en ola de
arriba hacia abajo y **la pantalla nunca queda vacía**.

Desde la 1.4.0 eso vale también **entre apps**: las bandas de los slots
**teselan las 480 filas**, o sea que entre todas cubren la pantalla entera sin
dejar una fila sin dueño. Por eso noticias puede entrar encima del selector sin
limpiar nada primero, y por eso desaparecieron dos cosas que antes hacían falta:

- la contabilidad de renglones huérfanos de `ui_ferced.cpp` (`s_prevLines`,
  `s_prevStatus` y el borrado a mano): un renglón que quedó vacío porque el
  titular nuevo es más corto **no se saltea, se limpia en su turno**, dentro de
  la ola;
- el `uiAnimInvalidate()` de `ui_padel.cpp` al cambiar de modo, que costaba
  99 ms de panel en negro.

Teselar no es gratis: los huecos que antes no repintaba nadie ahora se los
reparten los slots de los extremos, que pasan de ~64 filas a 126 y 84. Con el
modelo medido el peor frame de noticias sube de 37,6 a 43,2 ms — cuatro frames
de los ~40 de una transición. Es lo que se paga.

### Cómo se calculan las bandas

**No las cierres a mano.** Se intentó y quedaron dos huecos: doce filas entre la
última línea y el riel, y —peor— una banda que arrancaba *debajo* del texto que
tenía que dibujar, así que el renglón salía cortado por la mitad. Ninguno de los
dos daba error de compilación y ninguno se veía mirando esa pantalla sola.

`ui_padel.cpp` lo resuelve por construcción: cada slot declara dónde está su
contenido con `banda(top, alto)` —cuatro píxeles de aire arriba, y abajo el alto
del renglón **más `UI_RISE_PX`**, porque el elemento se dibuja 22 px más abajo
mientras entra— y después `cerrarHuecos()` estira los arranques hasta tocar el
cierre del anterior. Un hueco deja de ser posible, y ninguna banda se recorta por
debajo de su contenido. **Si tocás la geometría de una pantalla, usá ese patrón.**

Dos detalles que salieron de esto:

1. **El alto del renglón no es el tamaño en puntos.** Un titular de 19 pt ocupa
   40 px con las bajas incluidas. Cerrar la banda en 28 —el alto de una
   mayúscula— recorta los nombres con "j" o "g" durante toda la entrada.
2. **Las reglas no suben.** La regla del encabezado subía 22 px como el resto y
   terminaba fuera de su banda, dibujándose recortada. Ahora aparece en el
   lugar: es el eje de la composición, no un elemento que llega.

### Cómo verificarlo

Dos capturas, las dos del punto 6:

- **`-Congelar`** congela una transición a mitad de camino. Si en algún momento
  la pantalla queda vacía, se rompió la ola.
- **`-DesdeLauncher`** entra a la app pasando por el selector. Si alguna banda no
  cubre su parte de las 480 filas, queda un jirón del selector —que es una
  pantalla llena y clara— sobre el fondo de la app nueva, y se ve de lejos. **Los
  dos huecos de arriba aparecieron sólo así.**

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

### El chasis compartido — `ui_chrome.h`

Todas las pantallas se cuelgan de la misma estructura, y las medidas viven en
`namespace FercedChrome`:

```
CEJILLA en versalitas                                     [marca]
──────────────────────────────────────────────── regla    y = 76
(cuerpo)                                                  desde y = 104
════════════════════════════════════════════════ riel     y = 452
```

Margen estándar **28 px** en todo salvo configuración, que usa 32. Noticias es
la única que baja la regla a 112, porque su cabecera lleva la miniatura de 64 px.

### El espectro — la única cosa cromática del sistema

`fercedSpectrum(t)` devuelve el degrade del isotipo leído de arriba hacia abajo
—verde `#3FD95A`, cyan `#26CFC8`, azul `#2C97E6`— y tendido de izquierda a
derecha. Se interpola en 8 bits por canal y recién después se empaqueta a
RGB565: mezclando en 565 directo el verde pierde pasos y el degrade sale
escalonado.

**Esto no rompe la regla del acento: el espectro no significa estado, es la
marca.** Aparece siempre en el mismo renglón y en una sola forma, el riel de
`uiRail()`, así que no compite con `SUCCESS` ni con `DANGER`, que siguen siendo
los únicos colores que quieren decir algo.

El riel mide lo que cada pantalla tiene para medir: en noticias y pádel, cuánto
falta para el próximo ítem; en tareas, lo hecho sobre el total; en la lista de
avisos, lo leído; en la tarjeta que interrumpe, lo que queda hasta el cierre
automático; en el selector y en configuración va entero, porque ahí el aparato
habla de sí mismo. En una pantalla sin nada que medir queda apagado, que es lo
correcto.

**El espectro se estira sobre el tramo cumplido, no sobre el ancho total.**
Repartido sobre los 424 px, un riel al 20 % mostraba sólo verde y la marca no se
leía. Así el degrade entero está presente desde el primer píxel y lo que crece
es la escala.

### La marca

Hay dos, y no son intercambiables:

| Asset | Tamaño | Dónde |
|---|---|---|
| `data/ferced_mark_11.h` | 11x28, blanco | arriba a la derecha de las pantallas de **contenido** |
| `data/ferced_mark_c26.h` | 26x64, **a color** | encabezado del selector y de configuración |

La regla es: **el color pertenece a la voz de la marca**. En las pantallas de
contenido el protagonista es el titular y la marca se hace chica y monocroma; en
las dos donde el aparato habla de sí mismo, entra el isotipo entero con sus
cuatro planos.

La grande se genera desde el `logo.svg` del sitio con `tools/render_mark.py`,
que dibuja los cuatro polígonos con sus degrades sobre un lienzo 8x y promedia
al bajar. **Esto resuelve la limitación que este documento listaba como
insalvable** («no existe un asset más grande»): para otro tamaño,

```powershell
python tools\render_mark.py 26 64 ferced_mark_c26 src\data\ferced_mark_c26.h
```

El alto sale del ancho por la proporción del isotipo, 144x356 en el viewBox:
`alto ≈ ancho × 2,47`. `0x0000` es el centinela de «no pintar».

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
| `SatoshiMedium28` | 19pt | **0x20–0xFF** | `DS::fontTitular()` |
| `GeorgiaItalic28` | 18pt | **0x20–0xFF** | `DS::fontAcento()` |
| `GeorgiaItalic16` | 10pt | **0x20–0xFF** | `DS::fontAcentoSm()` |
| `SatoshiBold40` | 24pt | **0x20–0x7E** ⚠ | `DS::fontHero()` |
| `PPNeueMachinaBold24` | 24pt | **0x20–0x7E** ⚠ | (heredada de Lemon) |

**`fontTitular()` es la escala del aparato, y es nueva en la 1.4.0.** El titular
iba en `fontHeading()` —12 pt— y se leía como un párrafo: esto se mira de reojo
desde uno o dos metros, no de cerca como una web. Con 19 pt el titular llena el
cuerpo de la pantalla y desapareció el hueco muerto de 116 px que quedaba en el
medio de la pantalla de noticias.

**La cursiva con gracias es el gesto firma de ferced.com**, donde cada título
lleva una palabra en Instrument Serif dentro de una frase en Satoshi
(«Sistemas que *funcionan*.»). En el aparato cumple la misma función —marcar lo
que no es dato duro— en los accesorios: la hora relativa de un titular, el «día
4 de 8» de un torneo, los tres números vivos de configuración, los estados
vacíos («nada pendiente.»). **No hay Instrument Serif en bitmap, pero la propia
hoja de estilo del sitio declara Georgia como su reemplazo**
(`--font-instrument-serif: Georgia`), así que la versión del aparato sale de ahí
y no de una elección nueva.

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

Las Georgia salen de `C:\Windows\Fonts\georgiai.ttf`, que viene con Windows:

```powershell
python tools\ttf_to_gfx.py georgiai.ttf 18 src\data\GeorgiaItalic28.h GeorgiaItalic28 FF
```

**Cómo medir un ancho sin compilar nada.** En este proyecto los anchos van
medidos, no elegidos a ojo, y hasta la 1.4.0 medirlos exigía compilar el
simulador, dibujar y mirar la captura. `tools/medir_texto.py` lee el `.h` y suma
los `xAdvance`, que es exactamente lo que hace LovyanGFX:

```powershell
python tools\medir_texto.py src\data\GeorgiaItalic28.h "2 h 14 min" "35.4 fps"
```

También avisa si un carácter cae **fuera del rango** de la fuente, que es el
error que no da ningún síntoma salvo un glifo roto en pantalla. Los anchos de
las tres columnas de configuración salieron de acá: con tercios iguales el
encendido se recortaba a «2 h 14 m...» mientras al lado sobraban noventa píxeles.

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
.\shot.ps1 -Tareas -TareaScroll   # lista desplazada
.\shot.ps1 -Tareas -TareaDetalle  # detalle de tarea
.\shot.ps1 -Recordatorio          # recordatorio encima de otra app
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
los 420 ms**. Lo que tiene que verse: el titular nuevo entrando arriba **y los
renglones del anterior todavía en pantalla**. Si la pantalla queda vacía, volvió
el parpadeo.

Y de un ítem largo a uno corto con `-Congelar 2000` verifica que no queden
renglones huérfanos colgados.

**Para verificar que las bandas cubran las 480 filas**, que es lo otro que no se
ve mirando una pantalla sola:

```powershell
.\shot.ps1 -DesdeLauncher -Padel -Advance 0
```

Dibuja primero el selector —una pantalla llena y clara— y recién después entra a
la app pedida, que es lo que pasa de verdad cuando el usuario elige una app. Si
alguna banda no cubre su parte, en la captura queda un jirón del selector sobre
el fondo de la app nueva. **Los dos huecos que tenía la 1.4.0 en desarrollo
aparecieron sólo así**, y uno de ellos —una banda que arrancaba debajo del texto
que tenía que dibujar— cortaba un renglón por la mitad.

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

**Ojo: `sim/data/` está en `.gitignore`.** Los fixtures son locales, así que los
casos agregados a mano se pierden al regenerarlos o al clonar de nuevo. Dos que
conviene volver a poner en `padel.txt`, porque el orden de juego que devuelve la
fuente son **todos partidos por jugar** y sin ellos no hay forma de ver ni el
marcador ni al perdedor bajado a `FG_3`:

```
# partido terminado, nombres largos, resultado a tres sets
M|11:30|CENTER COURT|R32|M|Santiago Jose Pineda Cabello|Javier Ruiz Gonzalez|Agustin Tapia|Arturo Coello|3|1|6 4 7|4 6 5|2
```

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

### Noticias — `ui_ferced.cpp` (con ola, 7 slots)

```
 28  cabecera: miniatura 64x64 redondeada + fuente (fontHeading FG_2)
                                          + cuánto hace (cursiva FG_3)   marca →
112  regla
150  titular, hasta 5 renglones, fontTitular FG, LH 46, centrado en la grilla
452  riel del espectro: cuánto falta para el próximo ítem
```

Slots: 0 = cabecera, 1–5 = las cinco filas de la grilla, 6 = riel.

**La miniatura subió de pie a cabecera.** Era el único elemento gráfico del
aparato y estaba abajo a la izquierda ocupando el 1,7 % de la pantalla; ahora es
el sello de la fuente, arriba, junto al nombre y al «hace 2 d». Las esquinas se
redondean tapando los cuatro arcos con el canvas (`uiRoundCorners`), porque
`pushImage` no tiene forma de dibujarse redondeado.

**Sin miniatura va la inicial de la fuente en un cuadrado `SURFACE` del mismo
tamaño.** La alternativa era correr el texto al margen, pero entonces la
cabecera cambiaba de forma según el ítem: la mitad de los titulares del feed no
traen imagen.

**El bloque de texto se cuelga de una grilla de cinco filas y se corre para
quedar centrado.** Como el corrimiento es en pasos de un renglón entero, cada
línea sigue cayendo en una fila de la grilla y las bandas del escalonado no se
mueven: un titular de dos renglones y uno de cinco se animan igual. Una fila sin
contenido no se saltea, se limpia en su turno.

El texto se corta **por ancho real en píxeles** (`wrapText`), no contando
caracteres: con fuente proporcional contar caracteres deja renglones desparejos.

En reposo repinta 4 veces por segundo sólo el riel —dos renglones, no la
pantalla— para que avance suave sin costo perceptible.

### Pádel — `ui_padel.cpp` (con ola, 7 slots, dos modos)

Modo partido: `34` cejilla (torneo · fase · género) → `76` regla → `96` cuándo
(fecha · hora) → `150/190` pareja A → `246` regla corta → `268/308` pareja B →
`378` pie (cancha, y el día del torneo a la derecha en cursiva) → `452` riel.

La jerarquía responde a preguntas: **cuándo arriba, qué en el medio, dónde al
pie**. Las dos parejas son lo único en `fontTitular()`. El resultado va a la
derecha en `fontDataLg()`, ocupando el alto de las dos líneas; el perdedor de un
partido terminado baja entero a `FG_3`, que es la forma más barata de que se lea
quién ganó.

**El resultado y el número de cabeza de serie se reservan su columna antes de
escribir los nombres.** Con apellidos largos el nombre llegaba al borde y se
montaba encima del marcador. El seed sólo le descuenta ancho al primer nombre,
que es al lado de quien va: descontárselo también al segundo lo recortaba sin
motivo.

Modo torneo: nombre en hasta dos renglones en `fontTitular()`, la cuenta
regresiva en `fontAcento()` —la cursiva con gracias, que es el dato duro de la
pantalla— y rango de fechas y sede en Satoshi12.

Las filas de cada modo se arman con `banda()` y `cerrarHuecos()` al preparar la
pantalla, así el cálculo de banda sucia no tiene que saber en qué modo está. Ver
el punto 3.

### Selector de apps — `ui_launcher.cpp` (con cortina)

Encabezado con el **isotipo a color 26x64** + wordmark «FERCED» en
`fontDataLg()`, regla en `112`, y una grilla de baldosas.

**La grilla es de dos columnas** (`GRID_Y0 130`, `GRID_FIN 404`, `GAP 20`).
Antes era una lista de una columna con la altura repartida entre las apps, y con
la cuarta las tarjetas bajaban a 60 px y el bloque de texto se cruzaba con el
borde. Con dos columnas **entran seis sin apretar nada**, y `UI_LAUNCHER_MAX_APPS`
subió de 4 a 6.

La baldosa cambia de disposición sola: con 100 px o más de alto el monograma va
arriba a la izquierda y el texto abajo; por debajo pasa a horizontal —monograma
a la izquierda, texto a la derecha—, que es lo que hacía la lista vieja. Con
cuatro apps la baldosa mide 127 px, o sea la vertical; la horizontal aparece
recién con cinco o seis.

La app activa se dibuja con relleno `SURFACE`, borde `FG_2` y el monograma en
negativo. Los puntos de posición se fueron: con la grilla, cuál está activa se
ve sin ayuda.

### Tareas — `ui_todo.cpp` (con cortina)

Cejilla «TAREAS» + cuántas quedan a la derecha. Las filas tienen altura
variable: el título se envuelve por ancho real y **nunca usa puntos
suspensivos**. La lista tiene scroll vertical, subtareas independientes y un
control para colapsarlas. Hechas: casilla rellena con un tilde dibujado a mano.
**El riel del pie mide lo hecho sobre el total.**

Tocar el cuerpo abre el detalle: título completo, vencimiento, hora del aviso,
descripción y subtareas, todo desplazable. La casilla sigue completando sin
obligar a entrar.

El recordatorio es una tarjeta `PAPEL` que se compone encima de cualquier app y
detiene su tick mientras está abierta. Tiene tres blancos táctiles de al menos
44 px: `COMPLETAR`, `+10 MIN` y `ABRIR`.

Lista vacía: «nada pendiente.» en `fontAcento()`.

Al pie, la dirección para editarla desde el teléfono. **Se le pasa armada desde
afuera** (`uiTodoDraw(direccion)`) porque esta pantalla no consulta WiFi: así
también compila en el simulador.

### Avisos — `ui_notif.cpp` (con cortina, dos vistas)

**El aviso que llega no es una pantalla: es alguien que asoma.** El personaje
—la marca del agente a 56 px— se para abajo a la derecha y el globo de diálogo
sale de él hacia arriba y a la izquierda, con la cola apuntándolo. Arriba se
sigue viendo el titular, el partido o la lista que estabas mirando.

Puede dibujarse encima porque **todas las pantallas componen sobre el sprite**:
el sprite ya tiene lo que está en el panel, así que alcanza con pintar el globo
arriba y no hay que borrar ni redibujar nada. Con la arquitectura anterior esto
no se podía hacer.

- Globo en `PAPEL`, que es `FG`. No inventa un tono: el aparato ya usa el
  negativo —relleno blanco, texto en el canvas— para la app activa y la tarea
  hecha. Los grises sobre papel se derivan mezclando contra él, igual que los
  grises sobre canvas se derivan mezclando contra el canvas.
- Mensaje en `fontTitular()`, **dos renglones como mucho**. El detalle sólo
  aparece si el mensaje entró en uno: si no entra en dos, el aviso estaba mal
  escrito y para eso está la lista.
- **✕ redonda para cerrar**, dibujada a mano como el tilde de las tareas.
- **Nueve segundos**, no veinticinco. Y antes lo cerraba **cualquier** toque, así
  que un roce se llevaba el aviso antes de que llegaras a leerlo; ahora cierra la
  ✕, un deslizamiento —que es deliberado— o el tiempo.
- El riel del espectro se agota hasta el cierre y se repinta solo cuatro veces
  por segundo: son dos renglones, no la pantalla.

**Las marcas de los agentes** (`ui_chrome.h`) salen de los íconos oficiales:
Claude de `claude.ai/images/claude_app_icon.png` y OpenAI del CDN de
`cdn.oaistatic.com`. Los dos vienen como cuadrado opaco de dos tonos, así que
`tools/marca_agente.py` extrae **la forma** —un byte de alfa por píxel— y el
color lo pone el firmware: por eso la misma marca sirve encendida cuando el
aviso está sin leer y atenuada cuando ya se leyó, sin guardar dos copias. Claude
va en su coral `#D97757`; el nudo de OpenAI es negro sobre blanco, o sea
invisible sobre el canvas, así que va en blanco, que es su tratamiento para
fondo oscuro. Tres tamaños por marca —20, 34 y 56— son 8,6 KB de flash.

`src` es texto libre de hasta 14 caracteres de quien postea a `/api/notify`, así
que **el respaldo no es opcional**: una fuente sin marca cae en su inicial dentro
de la misma burbuja.

Lista: **5 filas de 62 px**. Eran 6 de 54, y así el título de un aviso —26 px en
`fontHeading()`— terminaba a 4 px de la fuente del siguiente y la lista se leía
como un bloque; la sexta se cuenta igual en el «+N más». Punto lleno = sin leer,
hueco = leído. El riel mide lo leído sobre el total.

### Aprovisionamiento — `ui_provision.cpp` (con cortina)

La primera pantalla que ve alguien que enchufa el aparato. Isotipo a color +
wordmark, el nombre del aparato a la derecha, regla en `98`, el QR sobre su
tarjeta blanca, la instrucción centrada y el riel entero del espectro.

**Es la única pantalla del aparato que centra el texto**, y es a propósito: es un
cartel, no una pantalla de contenido, y el QR ya manda el eje al centro.

El QR va sobre papel blanco porque es lo que los lectores esperan: invertido
muchos teléfonos no lo enganchan.

**El dibujo no sabe nada de redes.** `wifi_provision.cpp` levanta el AP y resuelve
el QR, y le pasa los módulos ya calculados. Por eso el simulador puede componer
esta pantalla, que era la única que no se podía mirar sin flashear.

### El nombre del aparato

Vive en NVS (`nvsGetNombre`/`nvsSetNombre`) y lo elige quien lo configura, desde
un campo opcional del portal cautivo. **Sobrevive a un reaparear**: al borrar el
WiFi el aparato vuelve al QR, pero sigue siendo el mismo aparato y se sigue
llamando igual.

Aparece en tres lados, y en los tres reemplaza a algo que ya estaba: el rótulo de
la derecha en configuración, el de la pantalla de setup, y **el SSID de la red de
apareo**, que pasa de `Ferced-Setup` a `Ferced-Fran`. Ese último es el que más
sirve: con dos cajas en la misma casa es la diferencia entre saber cuál es y
adivinar.

Para dejar un aparato listo para regalar hay un entorno de compilación de **un
solo uso** —`ferced_display_regalo`— que le pone nombre y le borra tareas, avisos
y WiFi. No se deja puesto: `tools/preparar_regalo.ps1` guarda primero la lista de
tareas del dueño anterior, flashea el de regalo, y vuelve a dejar el normal.

### Configuración — `ui_config.cpp` (con cortina)

Margen 32. Encabezado con el isotipo a color y el wordmark, regla en `100`,
**los datos en dos columnas** desde `118`, los tres números vivos en `244`,
franja de estado en `322` y cuatro botones de 44 px en dos filas.

**Era un volcado de diagnóstico**: nueve renglones iguales de etiqueta y valor,
todos con el mismo peso, sin decir cuál mirar primero. Ahora la etiqueta va
chica arriba en versalitas y el valor debajo —el peso al revés de como estaba—,
y los datos se agrupan por tema en dos columnas: identidad a la izquierda, red a
la derecha. **No se fue ningún dato.**

Los tres números que se mueven mientras mirás la pantalla —encendido, framerate,
ítems— viven en un bloque aparte y en `fontAcento()`. Es el único bloque vivo de
una pantalla que por lo demás dice cosas fijas.

**Los anchos están medidos**, no elegidos. Botones, con SatoshiMedium18: 162
(«Actualizar feed»), 217 («Buscar actualización»), 69 («Cerrar») y 110
(«Reaparear»). La columna ancha de 248 le deja 15 px de aire al más largo.
«Reaparear WiFi» medía 163 y se desbordaba de la pastilla angosta: el rótulo
perdió el sustantivo, no el verbo. Números vivos, con GeorgiaItalic28: 161
(«2 h 14 min», peor caso «999 h 59 min» = 204), 121 («35.4 fps») y 41 («20»,
peor caso 60). Todos salen de `tools/medir_texto.py`.

La franja de estado repinta **sólo su rectángulo**, y su barra de progreso lleva
el espectro de la marca: es el mismo riel que marca el paso del tiempo en las
demás pantallas, midiendo acá lo que baja el OTA. Informa hasta 101 veces: con
un repintado completo serían diez segundos de dibujo.

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

Cuatro de los siete puntos que listaba este documento se resolvieron en la
1.4.0: la transición entre apps (cortina), el selector sin animar (entra con
cortina), la marca en un solo tamaño (`tools/render_mark.py`) y el tope de
cuatro apps (grilla de dos columnas, seis). Lo que sigue abierto, en orden de
cuánto se nota:

1. **El framerate de la 1.4.0 no está medido en el aparato.** Los números del
   punto 1 son del firmware anterior. La escala tipográfica nueva y las bandas
   que teselan cambian el costo por frame —el modelo dice que el peor sube de
   37,6 a 43,2 ms— pero **eso es el modelo, no una medición**. Hay que flashear
   y leer la línea `[anim]`. Si el `period` se fue muy por encima de 28 ms, los
   candidatos son las dos bandas de los extremos de `ui_ferced.cpp`
   (`HEAD_FIN` y `RAIL_INI`).
2. **La cortina tampoco está medida.** Ocho empujes de 60 px deberían dar ~265
   ms; en el simulador se ve bien, pero el simulador reproduce el modelo, no el
   aparato.
3. **El titular a 19 pt recorta más nombres en pádel.** Con los nombres reales
   —que el proxy ya entrega abreviados, «S. Pineda Cabello»— entra casi todo;
   con uno sin abreviar, no. Si molesta, el lugar de arreglarlo es el proxy.
4. **Queda framerate sobre la mesa.** El período es 28,0 ms y el del panel 23,6:
   ~18% de los frames se pasan del presupuesto y esperan al flanco siguiente.
   Recortar un par de ms del trabajo por vuelta metería esos frames en un solo
   período y empujaría hacia los 42 fps del techo. `freq_write` a 16 MHz daría
   56 Hz de panel pero aumenta la contención de PSRAM. **Sin medir, no.**
5. **La pantalla de aprovisionamiento nunca se verificó en el panel.** El dibujo
   sí se verificó: se partió en dos —`wifi_provision.cpp` levanta el AP y
   resuelve el QR, `ui_provision.cpp` lo dibuja sobre el sprite recibiendo los
   módulos ya calculados— y el simulador compila la misma librería de QR que el
   firmware, así que `.\shot.ps1 -Setup` muestra el código **de verdad** y hasta
   se puede escanear del monitor. Lo que falta es mirarla en el panel.
6. **La página web de tareas** (`src/web_server.cpp`, en PROGMEM) es la otra
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
- **Los anchos van medidos.** Con `tools/medir_texto.py` cuesta un comando, así
  que no hay excusa. Tres recortes de la 1.4.0 —el encendido, el framerate y un
  nombre de pádel— salieron de estimar a ojo y errarle por tres píxeles.
- **Los huecos entre bandas no se cierran a mano.** Ver el punto 3: cada slot
  declara su contenido y el cierre se calcula. Sumar los `y0` a mano ya falló
  dos veces en una sola tarde.
- **El proxy resuelve las imágenes.** Baja, recorta a cuadrado, escala y
  convierte a RGB565. El ESP32 sólo pinta píxeles.
- **`pushImage` con un `uint16_t*` pelado asume orden intercambiado** y la
  imagen sale con los colores rotos. Hay que castear a
  `(const lgfx::rgb565_t*)`. **`readRect` tiene exactamente la misma trampa y en
  el mismo sentido**: leer crudo y empujar como `rgb565_t` devuelve los colores
  rotos. El filo de la cortina lee y escribe la misma franja del sprite, así que
  ahí el cast va en los dos lados.
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

### Si el aparato dice «no se pudo conectar o el TLS falló»

Ese texto es literal de `explicarHTTP()` en `ota_ferced.cpp`, y es el caso
`HTTPC_ERROR_CONNECTION_REFUSED` (-1). **El -1 no distingue** entre «no hay ruta
al host» y «el certificado no valida»: son el mismo número. Por eso ahora está
`porQueFallo()`, que imprime por serie el código de mbedTLS y el heap libre.
`-0x2700` es `X509_CERT_VERIFY_FAILED`, o sea la cadena.

Lo que pasó el 2026-08-06, por si vuelve: Let's Encrypt está migrando a las
raíces de la generación Y y `feed.ferced.com` ya sirve una cadena de cuatro

```
feed.ferced.com → YE2 → ISRG Root YE → ISRG Root X2 (firmada por X1)
```

Con X1 y X2 pineadas eso *debería* validar igual, porque termina en algo que
encadena a X1. Se agregaron YE e YR de todos modos —`tools/fetch_le_roots.py`
ya las baja— porque acortan el camino y porque el día que LE deje de
cross-firmarlas el aparato se queda sin poder actualizarse, que es la peor
forma de enterarse: rompe justo el mecanismo con el que se arregla.

**Ojo con el orden.** El arreglo del OTA viaja *dentro* del firmware nuevo, así
que si el OTA está roto no hay forma de que se arregle solo. Ahí es USB una vez
y listo. El feed no sirve de contraejemplo: usa `setInsecure()`, así que puede
andar con la cadena rota.

Uso actual: **19,8% de flash, 24,6% de RAM**. Hay lugar de sobra.

---

## 12. Mapa de archivos

| Ruta | Qué es |
|---|---|
| `src/ui_anim.cpp/.h` | Motor compartido: sprite, bandas, VSync, cortina, telemetría, paleta |
| `src/ui_chrome.cpp/.h` | Chasis: cejilla, regla, riel del espectro, marca, recortes |
| `src/ui_ferced.cpp` | App de noticias (con ola) |
| `src/ui_padel.cpp` | App de pádel (con ola) |
| `src/ui_todo.cpp` | App de tareas (con cortina) |
| `src/ui_notif.cpp` | Avisos: tarjeta que interrumpe + lista (con cortina) |
| `src/ui_launcher.cpp` | Selector de apps (con cortina) |
| `src/ui_config.cpp` | Configuración y franja de estado del OTA (con cortina) |
| `src/design_system.h` | Accesores de fuente, escala de espaciado, radios, altos de botón |
| `src/data/Satoshi*.h`, `Georgia*.h` | Las fuentes, generadas |
| `src/data/ferced_mark_11.h` | La marca chica, 11x28, blanca |
| `src/data/ferced_mark_c26.h` | El isotipo a color, 26x64 |
| `src/ui_provision.cpp` | La pantalla del QR (con cortina) |
| `src/wifi_provision.cpp` | AP, portal cautivo y el QR; ya no dibuja |
| `src/data/marca_*_20/34/56.h` | Las marcas de los agentes, máscaras de alfa |
| `tools/marca_agente.py` | Convierte el ícono de un agente a máscara de alfa |
| `tools/preparar_regalo.ps1` | Deja el aparato con nombre y sin datos, para regalarlo |
| `src/ferced_main.cpp` | Máquina de fases, gestos, qué app corre, quién pide la cortina |
| `src/touch_manager.cpp` | Clasificación de gestos |
| `src/web_server.cpp` | La página de tareas (en PROGMEM) y la entrada de avisos |
| `tools/render_mark.py` | Regenera el isotipo a color desde el `logo.svg` del sitio |
| `tools/medir_texto.py` | Ancho de un texto en una fuente, sin compilar nada |
| `sim/build.ps1` | Compila el simulador; acá se agregan los `ui_*.cpp` nuevos |
| `sim/shot.ps1` | Compilar + correr + capturar, un comando |
| `docs/HANDOFF.md` | El traspaso general: proxy, datos, OTA, VPS |

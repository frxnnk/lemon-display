# ferced-display — firmware de Ferced con feed social

Fecha: 2026-08-02
Estado: diseño validado, pendiente plan de implementación

## Qué es

Un firmware para la Lemon Box (MaTouch ESP32-S3 4.0", 480x480) con identidad
de Ferced, que muestra un feed rotativo de tweets y titulares actuales a razón
de 3-4 por minuto. Reemplaza por completo a las pantallas de crypto: no es un
modo más del firmware Lemon, es un firmware propio.

## Decisiones tomadas

| Tema | Decisión |
|---|---|
| Repo | Fork propio `ferced-display`, basado en `codex/v2-display-stability` |
| Alcance | Solo feed. Se sacan todas las escenas de crypto |
| Fuentes | Lista pública de X (vía Sorsa) + RSS, mezcladas en una rotación |
| Arquitectura | Proxy en el VPS; el firmware nunca sabe de dónde salen los datos |
| Cadencia | Pool de 30 ítems, uno cada 17 s, refresco cada 10 min |

### Por qué un fork y no una rama del repo actual

El repo `frxnnk/lemon-display` no es nuestro y su OTA apunta a los releases de
esa cuenta. Para publicar releases propios hace falta repo propio. Además la
decisión de sacar las escenas de crypto no tiene sentido aguas arriba.

La base es `codex/v2-display-stability`, no `master`: es la rama de la que
salió el release v5.1.1-beta.74 que corre hoy en el equipo, y está 25 betas
adelante de master.

## El spike que cambió el diseño

El diseño original usaba la API de sindicación de X (`cdn.syndication.twimg.com`),
que es gratis y sin API key. El paso 0 fue verificarla antes de escribir
firmware. Resultado, medido el 2026-08-02:

| Endpoint | Resultado |
|---|---|
| `tweet-result?id=…` | Responde. JSON con texto, fecha, autor, handle, idioma |
| `timeline/profile` | 200 con `Content-Length: 0` |
| `widgets/timelines/profile` | 200 con `Content-Length: 0` |
| `timeline/list` | 200 con `Content-Length: 0` |
| `srv/timeline-profile` | Timeout |

Los cuerpos vacíos no son un problema de red: las cabeceras son legítimas de X
(`x-served-by: cache-eze…-EZE`, `cache-control: no-cache, no-store`). X sirve
vacío a propósito para pedidos sin autenticar. La documentación de `twittxr` lo
confirma: el timeline exige cookies de sesión (`auth_token`, `ct0`).

**Conclusión**: se puede renderizar un tweet conociendo su ID, pero no se pueden
descubrir tweets nuevos. Descubrir es justamente el punto del producto.

Se descartó explícitamente mandar un `auth_token` de sesión: viola los términos
de X, expone la cuenta real a suspensión y la cookie vence a mano. Inaceptable
para un aparato prendido 24/7.

### Contexto de precios (agosto 2026)

X pasó a pay-per-use el 2026-02-08 y discontinuó el tier gratuito. Lectura a
USD 0,005 por post (USD 5 cada 1.000). El home timeline —descartado— habría
costado entre USD 75 y 225 por mes, porque sólo lo sirve la API oficial: exige
user context OAuth y ninguna reventa lo cubre.

La Lista de X vía Sorsa (`GET /v3/list-tweets`, hasta 20 tweets por llamada,
header `ApiKey`, sin OAuth) queda en el orden de centavos a un par de dólares
mensuales a esta cadencia. Restricción: **la Lista debe ser pública**; las
privadas no las alcanza ninguna API.

## Arquitectura

```
Lista pública de X ──┐
                     ├──→  feed-proxy (Go, VPS)  ──→  ESP32-S3 ferced-display
Feeds RSS ───────────┘      · mezcla cronológica       GET /v1/feed?n=30
                            · normaliza texto           JSON ~5 KB
                            · cachea 10 min
```

El firmware pide una URL y recibe texto listo para dibujar. Todo lo frágil
—claves, formatos de terceros, limpieza de texto— vive en el VPS, donde se
arregla sin reflashear.

### El proxy

Go, detrás del Caddy que ya corre en el VPS. Expone `GET /v1/feed?n=30`.

Interfaz `Source` con dos implementaciones:

- `sorsa.ListSource` — una llamada a `/v3/list-tweets`, header `ApiKey`.
- `rss.Source` — feeds configurables, sin credenciales.

Responsabilidades:

1. **Mezcla** ambas fuentes en orden cronológico, con marca de origen por ítem.
2. **Deduplica** por ID (tweets) y por URL (RSS).
3. **Cachea** 10 minutos; si una fuente falla, sirve lo último bueno.
4. **Normaliza el texto** — el punto crítico, ver abajo.

### Normalización: por qué va en el proxy y no en el firmware

La fuente embebida del firmware es ASCII-only y descarta todo codepoint no ASCII
(`news_client.cpp:139-144`). Sin tratamiento, un tweet en español pierde letras.

El proxy translitera (`á→a`, `ñ→n`), saca emojis, colapsa URLs a `[link]` y
recorta a ~180 caracteres. En Go son treinta líneas; en C++ sobre un MCU con
memoria contada sería doloroso y difícil de testear.

Resolverlo de verdad —mostrar "años" y no "anos"— es regenerar la fuente a
Latin-1 con `tools/ttf_to_gfx.py`, que el repo ya trae. Queda para etapa 2.

## El firmware

**Se va**: `ws_binance`, `stocks_client`, las partes de CoinGecko y Polymarket
de `api_client`, `ui_dashboard`, `ui_stocks`, `ui_v2_demo`.

**Se queda**: `display_manager`, `touch_manager`, `wifi_manager`,
`wifi_provision`, `nvs_storage`, `config_server`, `ota_manager`, `scheduler`,
`audio_manager`, `time_manager`, `design_system.h`.

**Nuevo**: `feed_client.*` y `ui_ferced.*`.

`feed_client` es un calco de `news_client.cpp`, que ya resuelve el patrón:
GET con límite de bytes, parseo a mano sin librería de JSON pesada, caché con
TTL, y degradación a caché viejo ante fallo. Es la pieza de menor riesgo.

### Regalo del relevamiento

`design_system.h` ya usa **Satoshi**, que es exactamente la tipográfica de
Ferced (era la secundaria de Lemon). La tipografía no hay que tocarla: el
rebranding se reduce a color, layout, logo y copy.

## La pantalla

480x480. **Se conserva el lenguaje visual de la UI V2 actual** y se rebrandea:
no se reemplaza por una pantalla de texto plano. Lo que hace que la UI de hoy
se vea moderna son sus primitivas, y esas se reusan tal cual.

| Primitiva | Dónde está hoy | Para qué |
|---|---|---|
| `LGFX_Sprite` de pantalla completa en PSRAM | `ui_v2_runtime.cpp:88` | Doble buffer, cero parpadeo |
| `pushSprite` con `setClipRect` | `:224-250` | Redibujar sólo lo que cambia |
| `fillSmoothRoundRect` | `:273` | Píldoras y tarjetas antialiaseadas |
| `drawWideLine` con ancho float | `:165, 255-262` | Líneas y glifos suaves |
| `drawBitmapTransparent` | `data/market_icons.h:22` | Logo |

```
┌──────────────────────────────────────┐
│  ╭─────────────╮            ferced   │  chip píldora + wordmark 120x28
│  │ AHORA · @han│                     │
│  ╰─────────────╯                     │
│  ╭────────────────────────────────╮  │  tarjeta radio 16, bgCard
│  │  El texto del item, en         │  │  SatoshiMedium18, blanco
│  │  Satoshi, hasta cinco lineas.  │  │  wrap por ancho real
│  │                                │  │
│  │  Nombre Apellido               │  │  blanco 75%
│  │  hace 12 min                   │  │  blanco 55%
│  ╰────────────────────────────────╯  │
│         ● ● ○ ○ ○ ○ ○ ○              │  posición en el pool
└──────────────────────────────────────┘
```

### Marca

El firmware ya dibuja el wordmark de Lemon como bitmap RGB565 de 120x28
(`src/data/lemon_v2_logo_light_120.h`). Ferced ocupa el mismo lugar: se genera
`ferced_logo_white_120.h` desde `logo-white.png` con `tools/png_to_rgb565.py`,
que compositea sobre negro y usa `0x0000` como clave de transparencia — justo
el formato de ese PNG, que es blanco sólido sobre transparente.

### Color

`src/colors.h` ya tiene `struct ThemePalette` con ~55 tokens y
`Colors::setTheme()`. Ferced entra como **tercer tema**, no como reescritura:
`--canvas #0e1011` → `bgBase`, `--card #0a0a0a` → `bgCard`, `--fg #fff` →
`textPrimary`, secundario `rgba(255,255,255,.55)` → `textTertiary`.

Regla de la marca: no inventar grises, todo gris es blanco con alpha. Donde la
paleta Lemon usa `lemonGreen` de acento, Ferced usa blanco. Verde `#34d399` y
rojo `#f87171` quedan **sólo para estado**, nunca decorativos.

El grano de la marca se hornea en el fondo estático: animarlo en un MCU cuesta
y no se nota. El tracking del eyebrow va en `.17em`; con `.25em` las etiquetas
largas se parten en dos líneas.

## Cadencia

Pool de 30 ítems (~20 de la Lista, ~10 de RSS). Uno cada 17 segundos = 3,5 por
minuto. Pool refrescado cada 10 minutos. No repite hasta agotar el pool.

**La repetición es inevitable y es una propiedad del producto, no un defecto.**
3,5/min son 5.040 ítems por día; ninguna fuente razonable produce ese volumen.
Se disimula recalculando "hace X min" en cada dibujado, así el tiempo avanza
aunque el ítem se repita.

## Errores

| Situación | Comportamiento |
|---|---|
| Sin WiFi | Provisioning existente (QR + AP `Lemon-Setup`, a renombrar) |
| Proxy caído | Sigue rotando el pool cacheado, marca sutil de desconexión, backoff 5→60 s |
| Una fuente caída | La otra sigue sola; el proxy no falla entero |
| Feed vacío | Cartel de estado, reintento al tocar |

El backoff 5→60 s y el "conservar caché y reintentar" son exactamente el patrón
que el módulo de noticias ya usa en producción.

## Testing

- **Proxy**: tests de Go sobre normalización con fixtures reales (tildes,
  emoji, URLs, hilos, RT), y sobre la mezcla cronológica y el dedup.
- **Firmware**: `tools/test_feed_parse.py` con payloads de ejemplo, sumado a
  los `tools/test_*.py` existentes.
- **Pantalla**: build de demo offline con pool fijo, para iterar la UI sin red.

## Riesgos

1. **La Lista debe ser pública.** La selección de cuentas queda visible en el
   perfil. Mitigación si molesta: armarla desde una cuenta alterna.
2. **Dependencia de un tercero.** Sorsa puede cambiar precios o formato. La
   interfaz `Source` acota el daño a una implementación, y RSS queda como piso:
   si se corta la suscripción, el aparato sigue funcionando.
3. **Tildes.** Transliteradas en etapa 1. Fuente Latin-1 en etapa 2.
4. **Cambio de canal OTA.** El primer flasheo va por USB; después OTA contra el
   repo propio. La tabla de particiones del equipo ya quedó en el layout de
   16 MB (ver `output/release-beta74-*/ROLLBACK.md`).

## Etapas

**Etapa 1** — Proxy con las dos fuentes, `feed_client`, `ui_ferced`, tema
Ferced, OTA propio. Texto transliterado.

**Etapa 2** — Fuente Latin-1 con tildes, renombrar el AP de provisioning,
posibles métricas propias de Ferced como segunda escena.

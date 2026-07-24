# Lemon Box V2 real - arquitectura de la canary

Estado: implementado y publicado en el canal OTA V2 hasta `v5.1.1-beta.48`; validacion fisica final pendiente en una canary.

## Limite del cambio

V2 real es un tercer entorno de firmware:

- `matouch_esp32s3_40`: firmware normal existente.
- `matouch_esp32s3_40_v2_demo`: relato deterministico offline de 78 segundos.
- `matouch_esp32s3_40_v2_real`: home persistente, datos reales y navegacion tactil.

El entorno real reutiliza display/touch MaTouch, NVS, Wi-Fi/provisioning, NTP, audio, Binance WebSocket/backfill, API client, Yahoo stocks worker, Config Server/Studio y el watchlist existente. No modifica ni suplanta la demo.

## Flujo

`main.cpp` entrega el ciclo a `v2_runtime.cpp`. El runtime obtiene datos y arma un `V2RuntimeSnapshot`; `ui_v2_runtime.cpp` y `ui_v2_settings.cpp` solo dibujan ese snapshot. `v2_runtime_model.h` contiene navegacion, timeouts y freshness sin acceder a red ni display.

## Navegacion

- Home es persistente y no tiene timeout.
- Tap en la banda inferior o swipe a la izquierda abre Market Tape.
- Tap en el bloque BTC cambia entre BTC/USD, BTC/ETH, BTC/SOL, BTC/ARS y BTC/ORO; la seleccion queda en NVS.
- Tap en una fila real abre Contexto para ese simbolo.
- El chevron y el titulo de Market Tape vuelven a Home; en Contexto vuelven a Market Tape. El swipe a la derecha se conserva.
- El engranaje visible de Home abre Settings con un toque.
- En Settings, tocar `Pantalla`, `Datos` o `Dispositivo` cambia de pagina; el swipe horizontal se conserva y no hay submenus.
- Market Tape vuelve a Home a los 30 segundos, Contexto a los 45 y Settings a los 90.

## Settings

- Pantalla: brillo, 12/24h, sonido, rotacion de watchlist y par BTC principal.
- Datos: watchlist, Wi-Fi, noticias, cadencia e IP de Studio. Un doble long press en Wi-Fi dentro de cinco segundos borra solo la red guardada y reinicia provisioning.

### Recuperacion de Wi-Fi en V2

Si no hay una red guardada, el arranque abre directamente el QR real de provisioning. Si existe una red pero el intento inicial de 15 segundos falla, V2 muestra `CONFIGURAR OTRA RED` y sigue reintentando el SSID anterior en segundo plano. Un toque abre el mismo QR/captive portal existente. Las credenciales nuevas se guardan en NVS unicamente despues de confirmar `WL_CONNECTED`; un password incorrecto vuelve a la pantalla de recuperacion sin reemplazar la configuracion persistida.
- Dispositivo: firmware, RSSI/heap, actualizacion, uptime y rollback.

## OTA V2

La infraestructura de descarga y escritura OTA anterior se conserva, pero V1 y V2 tienen assets separados. El firmware V2 llama `otaCheckAsset` y acepta exclusivamente `firmware-v2.bin`; nunca toma el `firmware.bin` historico. Una release mas nueva sin ese asset se ignora.

La comprobacion completa se realiza al arrancar y cada seis horas como fallback. Desde beta.47, un probe `HEAD` al redirect publico de Releases consulta la etiqueta latest cada 60 segundos sin consumir continuamente la API; solo si detecta una version mayor ejecuta el chequeo completo. Para ofrecer la instalacion deben cumplirse simultaneamente version remota mayor, nombre exacto del asset y checksum especifico en el body con formato `firmware-v2.bin MD5: <32 hex>`. Sin checksum no aparece como instalable.

Cuando hay una version, Home muestra `ACTUALIZACION V...`. El primer toque arma una ventana de ocho segundos y cambia el texto a `TOCA PARA CONFIRMAR`; el segundo toque instala. La fila Actualizacion de Settings aplica la misma confirmacion de dos toques. La descarga detiene temporalmente WS, providers, Studio y el worker de stocks para liberar TLS/heap. Si falla, los servicios se reconstruyen; si termina, `Update` reinicia el ESP32.

## Datos, cache y freshness

| Superficie | Provider | Refresh | Cache | Estados |
|---|---|---:|---|---|
| BTC/USD | Binance kline WS + backfill REST; CoinGecko fallback/metadatos | live; metadata 5 min | memoria | live, cached, stale, offline, error, rate limit |
| BTC/ETH y BTC/SOL | Binance kline WS invertido + backfill REST | live | memoria | live, cached, stale, offline, error |
| BTC/ARS | Binance BTC/USD + CriptoYa Lemon USDC/ARS | live + cruce 30 s | memoria | live, cached, stale, offline, error, rate limit |
| BTC/ORO | CoinGecko `BTC/xau` | 5 min | memoria | live, cached, stale, offline, error, rate limit |
| Watchlist | Yahoo Finance `/v8/finance/chart` | burst inicial + ciclo conservador | NVS por simbolo | live, cached, stale, loading, error, rate limit |
| Hora | NTP existente, UTC-3 | 1 s visual | ultimo valor valido | `--:--:--` hasta sync |

Los valores restaurados de NVS se muestran `STALE` hasta que el provider confirma una actualizacion. No se presenta ningun fixture como cotizacion real.

## Estabilidad del panel RGB

El Home completo se dibuja al entrar o cuando cambia una escena. El reloj no invalida la escena, omite textos identicos y actualiza un clip de 108 x 30 px. En beta.48, selector en `y=82`, sparkline desde `y=106` y precio centrado visualmente en `y=144`; tarjetas en `y=258`. Market Tape y Ajustes son botones inferiores separados desde `y=370`, y Ajustes usa un icono de sliders. Los ticks del WS actualizan solamente el hero; el ciclo de stocks solo su tarjeta; y Dolar solo su tarjeta salvo cuando BTC/ARS necesita recalcular tambien el hero. Cada push espera VSync y recorta tanto el sprite fuente como el framebuffer destino. BTC/USD se muestra completo con centavos; K/M se reservan para cifras grandes.

## Noticias y Packs

Noticias quedan deshabilitadas como `Provider pendiente`. Para habilitarlas hace falta un provider verificable con fuente, timestamp, URL/deep link, TTL y politica de rate limit. Packs no se implementan porque no se encontro una API publica contractual. No se fabrican composiciones o precios.

## Intervenciones temporales

La base soporta escenas temporales y retorno automatico. En esta primera canary solo la navegacion abre Tape/Contexto; no se disparan aperturas, noticias o movimientos hasta disponer de reglas/provider testeables. La rotacion cambia el simbolo secundario de Home, no convierte la caja en slideshow.

## Rollback fisico

`tools/flash_v2_real_canary.ps1` valida puerto, VID, serial y tabla de particiones; compila; lee la app actual completa desde `0x10000`; guarda hashes; y solo entonces escribe V2 en `0x10000` con `keep/keep`. Deja un comando de rollback con el backup pre-flash y nunca borra flash.

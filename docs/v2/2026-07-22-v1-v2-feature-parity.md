# Lemon Box V1/V2 - matriz de paridad

Estado auditado sobre el firmware real. `V2 ahora` significa que la funcion esta conectada a datos reales en `matouch_esp32s3_40_v2_real`; no implica publicacion OTA.

## Datos y experiencia

| Capacidad del firmware anterior | V2 ahora | Decision |
|---|---|---|
| Hora por NTP | Si | Se mantiene en Home con actualizacion parcial cada segundo. |
| BTC/USD live por Binance WS | Si | Restaurado. Binance entrega el precio live y el backfill REST; CoinGecko queda para cambio 24h y fallback. |
| BTC/ETH, BTC/SOL | Si | Restaurados reutilizando los streams invertidos existentes. |
| BTC/ARS | Si | Restaurado como cotizacion derivada: BTC/USD de Binance por USDC/ARS de Lemon en CriptoYa. |
| BTC/ORO | Si | Restaurado con `BTC/xau` de CoinGecko, sin inventar conversiones. |
| Persistencia del par activo | Si | El par elegido se guarda en NVS como `v2_pair`. |
| Sparkline y periodos 5m, 15m, 1h, 4h, 24h, 1M, 6M, 1Y | No todavia | La infraestructura sigue en el firmware normal. Reintegrar como detalle de BTC, no en el Home ambiental. |
| Linea, marcadores y velas | No todavia | Mantener fuera de Home; candidato para una unica vista de contexto. |
| Dolar Lemon bid/ask | Si | Tarjeta propia Compra/Venta con provider real y freshness; el grafico queda reservado al activo principal. |
| Watchlist de acciones | Si | Yahoo + cache NVS, editable desde Studio; Home rota el foco. |
| Market Tape y contexto de accion | Si | Navegacion V2 real, sin titulares falsos. |
| Alertas sonoras por movimiento BTC | No todavia | Audio y preferencia siguen disponibles; falta definir umbral y feedback V2 para evitar alertas agresivas. |
| Estados loading/cache/stale/offline/error/rate limit | Si | Visibles discretamente por superficie. |

## Settings y operacion

| Capacidad anterior | V2 ahora | Decision |
|---|---|---|
| Brillo, 12/24h, sonido | Si | Acciones tactiles directas y persistidas. |
| Wi-Fi/provisioning/reset de red | Si | Se conserva; borrar Wi-Fi exige dos long press dentro de cinco segundos. |
| Watchlist/Studio | Si | Se conserva el Config Server y la watchlist en NVS. |
| Rotacion | Si | Cambia solamente el activo secundario; Home no se vuelve slideshow. |
| Tema claro/oscuro | No | V2 canary respeta una unica composicion de marca; evaluar despues de validar contraste fisico. |
| Layouts Normal/Pro | Reemplazado | V2 usa Home, Tape, Contexto y Settings; no porta layouts completos del dashboard viejo. |
| OTA GitHub | Si | Canal V2 separado por asset exacto y MD5; Home exige dos toques dentro de ocho segundos antes de instalar. |
| Diagnostico | Si | RSSI, heap, uptime, version y rollback preparado. |

## Fuera del alcance transaccional

Los nombres `trading pair` del codigo anterior describen pares de cotizacion. Ni V1 ni V2 envian ordenes de compra/venta desde esta ruta. V2 muestra datos de mercado y contexto; no ejecuta operaciones ni recomendaciones personalizadas.

Polymarket/predicciones queda disponible solamente en el firmware normal y no se reactiva en V2 sin una decision explicita de Producto/Legal. Noticias requieren provider verificable con fuente, timestamp, URL y TTL. Packs requieren una fuente contractual de composicion y rendimiento; no se fabrican datos.

## Siguiente bloque recomendado

Despues de validar fisicamente WS, pares y estabilidad del panel, recuperar una sola vista de detalle BTC con sparkline y selector de periodo. Luego sumar Dolar Digital como tarjeta propia. Evitar portar todo el dashboard Pro a Home.

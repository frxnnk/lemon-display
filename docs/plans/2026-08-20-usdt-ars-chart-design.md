# USDT/ARS 7D Chart Design

## Objetivo

Agregar a Markets un grafico ancho de precio USDT/ARS de siete dias usando la
misma respuesta de CoinGecko que ya alimenta las variaciones 1H, 24H y 7D.

## Diseno

El parser conserva una serie reducida de hasta 48 precios, muestreados de forma
uniforme desde `prices`. La serie vive en `UsdtPegData`, junto con las
variaciones que derivan de la misma fuente. No se agrega ningun endpoint ni
dependencia.

Markets mantiene arriba las cards `USDT / ARS` y `USDT / USD`. La fila inferior
se reemplaza por una card de ancho completo `USDT / ARS - 7D`, con linea de
precio, minimo, maximo y valor actual. El trazo es verde cuando el ultimo punto
es mayor o igual al primero y rojo cuando termina debajo.

Si la serie todavia no esta disponible, la card conserva su marco y muestra la
animacion de carga o `--`, sin bloquear la navegacion ni ocultar las dos cards
superiores.

## Verificacion

- Tests del contrato del parser para serie acotada y downsampling.
- Tests del contrato visual para card ancha, labels y ausencia de las dos cards
  inferiores anteriores.
- Suite completa y build limpio del entorno USDT.


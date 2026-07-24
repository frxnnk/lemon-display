# Lemon Box V2 - rebalance del hero de Home

## Problema observado

Al recuperar precision visible para BTC/USD en beta.46, el precio dejo de ocupar el ancho previsto por la composicion abreviada. El grafico permanecio en su posicion anterior, el hero quedo bajo y aparecio demasiado aire entre el imagotipo y la informacion principal.

## Composicion

- El imagotipo permanece en `x=32, y=32` y la hora a la derecha.
- La fila superior queda limpia con imagotipo y hora.
- El selector `BTC/...` comienza en `y=82`, inmediatamente debajo del header.
- El sparkline ocupa el bloque derecho desde `y=106`. El precio usa datum vertical centrado en `y=144`, alineado contra el centro real del grafico en vez de contra la caja tipografica.
- Variacion queda debajo del precio y freshness en el extremo inferior derecho del hero.
- Las tarjetas suben a `y=258`. En `y=370`, Market Tape ocupa el boton principal y Ajustes un boton secundario independiente a la derecha.
- Ajustes usa un icono de sliders dentro de un contorno cuadrado; deja de competir visualmente con el reloj o el selector de par.
- Si hay OTA, el aviso reemplaza temporalmente `TOCA PARA CAMBIAR` en el lado derecho del selector. No desplaza el hero.

## Interaccion y estabilidad

El boton de Ajustes usa una zona tactil propia en la barra inferior y se evalua antes que Market Tape. El hero completo sigue cambiando el par por toque; cuando existe una OTA, su aviso se procesa primero para conservar la confirmacion de dos toques. Los clips parciales mantienen recorte de sprite y framebuffer con VSync.

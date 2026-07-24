# Lemon Box V2 - estabilidad y paridad del Home

Estado: diseño validado para la segunda canary física.

## Problema confirmado

La primera corrección quitó el redraw completo del reloj y del tick de Binance, pero quedaron tres invalidaciones periódicas de pantalla completa: actualización de acciones cada 60 segundos, rotación del foco cada 15/30/60 segundos y metadatos de CoinGecko cada 5 minutos. En el panel RGB esos pushes 480 x 480 se perciben como salto o bounce.

V2 también inicia Wi-Fi y proveedores antes del primer `v2UiDraw`, por lo que durante el arranque no muestra progreso. El QR existente no desapareció: sólo se presenta cuando NVS no tiene una red Wi-Fi guardada.

## Home aprobado

- Hero superior en dos columnas: precio/variación a la izquierda y sparkline real del par activo a la derecha.
- Números largos usan notación compacta (`$105,4M`, `$65,9K`) para no invadir el gráfico.
- El sparkline reutiliza el backfill y buffer de Binance para USD, ETH y SOL; BTC/ARS transforma esa misma serie con el cruce Lemon. BTC/ORO muestra estado sin serie hasta tener una fuente histórica verificable.
- Debajo hay dos tarjetas de igual jerarquía: Dólar Lemon con Compra/Venta USDC/ARS, y la acción enfocada de la watchlist.
- Market Tape conserva una única acción inferior. La flecha textual se reemplaza por un chevron geométrico de dos trazos.

## Render y datos

Los cambios de datos no pueden marcar la escena completa como dirty. Se agregan clips separados para hero, tarjetas de Home, Tape y Contexto. Sólo navegación, entrada a Settings, cambios de página y transiciones de conectividad pueden dibujar un frame completo.

Lemon USDC/ARS se consulta siempre cada 30 segundos, no únicamente cuando el par BTC/ARS está seleccionado. Su tarjeta conserva el último valor visible y muestra freshness discreto.

## Arranque

V2 dibuja inmediatamente un loading de marca y actualiza sólo su barra/texto durante: Wi-Fi, hora, Bitcoin/Dólar, histórico y watchlist. Si no existe Wi-Fi guardado, pasa al QR real de provisioning. Si existe, conecta directamente y no pide escaneo ni reinicio.

## Prueba física

Observar Home por al menos 90 segundos para cubrir rotación y actualización de acciones. Validar además loading tras RESET, Compra/Venta de Dólar, sparkline, formato compacto, chevron, navegación y los cinco pares. El flash sigue siendo sólo app en `0x10000`, `keep/keep`, con rollback nuevo previo.

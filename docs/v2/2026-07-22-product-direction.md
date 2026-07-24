# Lemon Box V2 - Dirección de producto

Estado: borrador validado conceptualmente.

## Punto de partida

Lemon Box ya tiene valor como objeto de escritorio. Es linda, distintiva y permite tener a la vista el precio de Bitcoin, la hora, el dólar y acciones. Esa utilidad pasiva no debe perderse intentando convertirla en un terminal profesional o en una pantalla inteligente genérica.

La oportunidad de V2 es explotar mejor la presencia permanente de la pantalla: seleccionar información, cambiar según el momento del día y explicar acontecimientos sin exigir atención continua.

## Tesis

> Lemon Box V2 es un objeto financiero vivo: siempre útil de reojo, ocasionalmente sorprendente y más profundo cuando se lo toca.

Una formulación comercial posible es "Lemon en tu escritorio". Lemon Acciones es un momento especialmente relevante para el producto, pero la caja no debe limitarse a acciones. Puede conectar Bitcoin, Dólar Digital, acciones, índices, Packs y contenido editorial controlado por Lemon.

## Principio de atención

- 90% pasivo y visible de reojo.
- 10% táctil y exploratorio.
- La caja debe premiar la mirada, no pedir atención.
- Una pantalla debe contar una cosa principal por vez.

## Tres capas de experiencia

### 1. Ambiente

El estado normal muestra información comprensible en dos segundos: hora, activo principal, precio, variación y una señal secundaria. Debe conservar la calma necesaria para permanecer encendido todo el día.

### 2. Momentos

Un acontecimiento relevante ocupa temporalmente la pantalla y luego devuelve al usuario al estado normal. Ejemplos:

- Apertura o cierre del mercado.
- Bitcoin cruza un hito significativo.
- Un activo de la watchlist tiene un movimiento excepcional.
- Resultados o anuncio relevante de una empresa.
- Una noticia explica el movimiento que ya se observa.
- Un Pack tiene un comportamiento destacable.

### 3. Exploración

Al tocar, el usuario puede ver contexto histórico, entender por qué ocurrió un movimiento o continuar en Lemon mediante un QR o deep link. La cajita informa y deriva; no opera.

## Ritmo diario

- Mañana: hora, Bitcoin y agenda financiera breve.
- Pre-market: cuenta regresiva y principales movimientos.
- Apertura: transición visual y sonido breve opcional.
- Mercado abierto: activo principal, Market Tape ocasional y contexto.
- Cierre: resultado del día e historia principal.
- Noche: reloj, Bitcoin y una experiencia visual más quieta.
- Fin de semana: crypto, Dólar Digital, recap semanal y próximos eventos.

## Públicos

### Usuario habitual

Objeto interesante y estético para el escritorio. Prioriza Bitcoin, hora, dólar, watchlist y noticias importantes.

### Oficina Lemon

Estado de mercado siempre encendido, información compartida y momentos editoriales.

### Prensa y embajadores

Experiencias simples de explicar, campañas y contenido compartible. No requieren una lógica de múltiples cajas conectadas entre sí.

### CEO Edition

Mismo software base y compatibilidad de flota, con configuración ejecutiva, terminación física premium y packaging propio. No debe convertirse en un firmware incompatible.

## Decisiones tomadas

- La demostración no depende de seis cajas sincronizadas.
- Una sola caja excelente es más importante que una coreografía de varias unidades.
- Varias cajas pueden servir para mostrar perfiles diferentes, pero no necesitan conectarse entre sí.
- Noticias y Market Tape forman parte del lenguaje de V2, no deben dominar todo el producto.
- La inteligencia principal es editorial y contextual: decidir qué mostrar y cuándo.
- El contenido explicativo debe prepararse fuera del dispositivo y llegar en un formato compacto y controlado.

## No objetivos

- Trading o ejecución de órdenes.
- Recomendaciones personalizadas.
- Un clon de Bloomberg o TradingView.
- Una pantalla genérica de clima, calendario, videos y noticias sin relación con Lemon.
- IA generativa o procesamiento de artículos en el ESP32.
- Alertas agresivas o estímulos permanentes.
- Voice assistant: el hardware no tiene micrófono integrado.
- Reescritura total del firmware.

## Primer prototipo recomendado

Sobre una caja existente:

1. Home de Bitcoin persistente pero más viva.
2. Ritmo de día y mercado.
3. Rotación controlada de watchlist, índices y Packs.
4. Un modo Market Tape.
5. Una tarjeta "Qué pasó" asociada a un movimiento.
6. Apertura, cierre e hitos con audio opcional.
7. Toque para contexto y QR solamente cuando aporte valor.
8. Fixtures determinísticos para que la demo no dependa de servicios externos.

## Preguntas abiertas

- Frecuencia con la que el home puede interrumpir Bitcoin para mostrar otro contenido.
- Qué fuentes y workflow editorial autoriza Lemon para noticias.
- Composición oficial y disponibilidad de datos de Packs.
- Deep links oficiales de Lemon y atribución permitida.
- Qué configuraciones deben vivir en Lemon y cuáles en una mini web.
- Qué parte de Polymarket permanece como Labs o sale de la experiencia central.


# Lemon Box V2 - Especificación de experiencias

Estado: diseño conceptual, pendiente de mockups y validación de marca.

## 1. Home ambiental

El home conserva un héroe principal, inicialmente Bitcoin, acompañado por hora, precio, variación y una señal secundaria. No debe comportarse como un carrusel automático indiscriminado.

La rotación inteligente puede mostrar temporalmente otro activo cuando exista una razón clara:

- Movimiento significativo.
- Apertura o cierre.
- Evento programado.
- Noticia relevante.
- Preferencia explícita del usuario.

Después del momento, la experiencia vuelve al héroe principal.

## 2. Market Tape

Market Tape toma como referencia las cintas de cotizaciones de Wall Street, adaptadas a una pantalla cuadrada pequeña.

### Usos recomendados

- Cinta inferior con pocos activos mientras el héroe permanece visible.
- Takeover breve de 15 a 30 segundos en apertura, cierre o cambio de modo.
- Modo ambiental opcional para oficina o usuarios intensivos.

Ejemplo conceptual:

```text
AAPL +1,8%   NVDA -2,1%   MELI +0,7%   BTC 118.420
```

### Restricciones

- Entre cuatro y ocho instrumentos, no veinte.
- Velocidad legible a distancia de escritorio.
- Números con ancho estable para evitar saltos visuales.
- No mostrar precio, volumen, máximo, mínimo y noticias simultáneamente.
- No usar movimiento continuo en todos los modos.
- No adoptar una estética de casino o alarma permanente.

### Conexión narrativa

La cinta puede detenerse en un activo, ampliar su símbolo y revelar una explicación:

```text
NVDA -4,2% -> nuevas restricciones sobre chips
```

El movimiento visual conecta dato y contexto; no es decoración aislada.

## 3. Noticias importantes

La caja no debe ser un portal de noticias. Una noticia merece interrumpir cuando cambia la forma de entender un número que ya está en pantalla.

### Criterios de relevancia

- Afecta un activo de la watchlist.
- Explica un movimiento importante.
- Es relevante para Bitcoin, Dólar Digital, índices o Packs.
- Es un evento programado: resultados, inflación, tasas, elecciones o regulación.
- Lemon la publica como contenido editorial destacado.

### Niveles

#### Flash

Titular de una línea durante 8 a 12 segundos.

#### Contexto

Al tocar, muestra el activo relacionado, el movimiento y por qué importa.

#### Continuar

QR o deep link hacia Lemon cuando existe un destino útil. El QR no permanece visible por defecto.

### Ejemplo conceptual

```text
NVDA -4,2%

Nuevas restricciones pueden afectar
las ventas de chips en China.

Por qué importa: parte del crecimiento
esperado depende de ese mercado.
```

La redacción debe ser factual, breve y no prescriptiva. No debe indicar comprar, vender o mantener.

### Producción del contenido

El ESP32 no resume artículos ni ejecuta IA. Recibe una tarjeta preparada por un servicio controlado:

- Identificador.
- Tipo de momento.
- Titular.
- Explicación corta.
- Activos relacionados.
- Prioridad.
- Audiencia.
- Inicio y vencimiento.
- Deep link opcional.
- Fallback y versión.

Si la tarjeta vence o falla su descarga, no se muestra. La caja mantiene la última experiencia segura y los precios cacheados.

## 4. Packs

Los Packs deben percibirse como una idea temática, no solamente como otra fila de tickers.

Una tarjeta puede mostrar:

- Nombre oficial del Pack.
- Pulso agregado.
- Principal impulsor del día.
- Una frase descriptiva aprobada.
- Composición resumida si Lemon autoriza esos datos.

No se deben inventar composiciones, rendimientos ni denominaciones. La fuente oficial debe provenir de Lemon o de su integración autorizada.

## 5. Contexto histórico

La caja puede enriquecer precios sin emitir recomendaciones:

- Máximo o mínimo de 30 días.
- Variación mensual.
- Movimiento desde el cierre anterior.
- Comparación con un índice.
- Próximo evento programado.

Cada métrica debe indicar período y fuente de forma inequívoca.

## 6. Sonido

El parlante se usa con moderación:

- Inicio del dispositivo.
- Apertura y cierre.
- Hito excepcional.
- Confirmación de interacción.

Debe existir volumen, mute y horario silencioso. Ningún evento editorial debe depender exclusivamente del sonido.

## 7. Interacción

- Mirar: entender el estado principal.
- Tocar: abrir contexto.
- Deslizar: cambiar entre pocos dominios claros.
- Volver: gesto o affordance visible y consistente.
- QR: continuar en Lemon.

V2 debe reducir la dependencia de gestos ocultos. Las interacciones avanzadas actuales pueden mantenerse en Labs, pero el recorrido principal debe ser descubrible.

## 8. Demo determinística

La primera demo debe poder reproducir una sesión completa sin depender de que el mercado esté abierto:

1. Pre-market y cuenta regresiva.
2. Apertura con Market Tape.
3. Movimiento de S&P 500 y Nasdaq.
4. Pack de IA.
5. Apple, Nvidia o Tesla.
6. MELI o YPF.
7. Noticia que explica un movimiento.
8. Cierre y QR.

Los precios y eventos de la demo deben estar etiquetados como simulación o escenario cuando no sean datos en vivo.


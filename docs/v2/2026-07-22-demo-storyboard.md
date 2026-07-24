# Lemon Box V2 — storyboard de demo física

Duración objetivo: 78 segundos. Una sola unidad existente. Datos determinísticos, sin depender de APIs ni de la hora real.

## Qué debe demostrar

La caja ya es linda en el escritorio. V2 no intenta convertirla en una Bloomberg Terminal: demuestra que puede decidir cuándo quedarse tranquila y cuándo usar la pantalla para explicar algo relevante.

La historia es: objeto ambiental → momento de mercado → pasarela → noticia → contexto → continuidad en Lemon.

## Guion segundo a segundo

| Tiempo | Pantalla | Acción visible | Relato sugerido |
|---:|---|---|---|
| 0–8 s | Home ambiental | Hora, BTC, variación y cuenta regresiva. Sin movimiento permanente. | “La mayor parte del día es un objeto calmo: hora, Bitcoin y lo próximo que importa.” |
| 8–15 s | Preapertura | La cuenta baja de `00:00:07` a `00:00:00`; pulso breve en el borde. | “No exige atención todo el tiempo; cobra vida cuando hay un momento.” |
| 15–19 s | Transición | Voxel corto construye `MERCADO ABIERTO`. Sin glow continuo. | “Abre Estados Unidos.” |
| 19–34 s | Market Tape | S&amp;P 500, Nasdaq, Apple y Nvidia avanzan una fila por vez. Flechas indican dirección. | “Una pasarela al estilo Wall Street, pero legible desde el escritorio.” |
| 34–43 s | Pack IA | El Pack IA ocupa la franja inferior y luego toma foco. | “También traduce el mercado a productos que ya existen dentro de Lemon.” |
| 43–58 s | Noticia | Lime sólido: `NVIDIA CAE 4,2%`, “qué pasó” y “por qué importa”. | “La diferencia no es mostrar más números: es explicar por qué uno de ellos importa.” |
| 58–67 s | Contexto | Un toque revela fuente, hora y dos bullets adicionales. | “Si quiero saber más, la caja me da contexto sin recomendarme qué hacer.” |
| 67–74 s | Continuar | Aparece QR/deep link atribuible y el texto `VER EN LEMON`. | “Y continúa en Lemon, donde vive la experiencia completa.” |
| 74–78 s | Retorno | Máscara suave vuelve al home. | “Después vuelve a ser la cajita linda del escritorio.” |

## Fixtures de demo

Usar un archivo local y congelado; ningún valor debe variar durante la reunión:

- Hora visual: `09:41`.
- BTC: `$ 118.420`, `+2,8%`.
- S&amp;P 500: `6.309,62`, `+0,54%`.
- Nasdaq: `21.083,32`, `+0,61%`.
- Apple: `227,16`, `+1,80%`.
- Nvidia: `171,38`, `-2,10%` en Tape y un escenario editorial separado de `-4,2%`.
- Pack IA: `+1,4%`.

La pantalla debe llevar `DEMO` o `DATOS DE DEMO`. La inconsistencia deliberada de Nvidia representa dos momentos narrativos distintos; para una demo pública conviene unificarla o indicar la hora de cada dato.

## Interacción física mínima

Una sola interacción es suficiente:

1. Toque durante la noticia.
2. Cambia de resumen a contexto.
3. Segundo toque o timeout muestra QR.
4. Timeout de 7 segundos vuelve al home.

No hace falta dashboard para ejecutar esta historia. El operador elige `demo_v2` desde una configuración local o un endpoint protegido; la secuencia corre sola.

## Plano de filmación de respaldo

Duración: 75–85 segundos, vertical 9:16 y una versión recortada 16:9.

- 0–6 s: plano general de la caja en un escritorio limpio.
- 6–15 s: acercamiento al countdown.
- 15–35 s: plano fijo frontal durante la apertura y Tape.
- 35–58 s: plano a 30° para Pack IA y noticia; evitar reflejos.
- 58–68 s: mano toca una vez la pantalla.
- 68–76 s: teléfono entra a cuadro y escanea el QR de demo.
- 76–82 s: regreso al plano general con home ambiental.

Grabar sin audio ambiente y agregar voz en off después. No filmar datos vivos ni un QR de producción.

## Criterio de éxito

La demo funciona si la persona puede explicar, sin ayuda:

1. Qué muestra cuando “no pasa nada”.
2. Qué cambió al abrir el mercado.
3. Por qué apareció una noticia.
4. Cómo continúa en Lemon.

Si la conversación se concentra en la cantidad de pantallas o en seis cajas sincronizadas, la demo perdió el foco.

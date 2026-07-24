# Lemon Box V2: loading progresivo de marca

## Objetivo

Recuperar el loading reconocible del firmware anterior: el imagotipo Lemon completo pasa de gris a verde mientras la caja inicia. La version V2 conserva sus estados reales de carga y elimina la barra generica.

## Comportamiento

- Usa el asset oficial existente `lemon_imagotipo_244` de 244 x 56 px.
- El logo comienza en escala de grises.
- Cada valor real enviado a `v2UiDrawLoading(status, progress)` revela el color de izquierda a derecha.
- Una franja de transicion de 18 px suaviza el frente del barrido.
- El estado actual aparece debajo: Wi-Fi, hora, Bitcoin, Dolar Lemon, historico y watchlist.
- Al 100 %, el imagotipo queda completamente en color.
- Se elimina la barra horizontal de progreso de V2.
- Las actualizaciones posteriores al primer frame usan un clip de 280 x 138 px con VSync para evitar refrescos completos y parpadeo.

## Compatibilidad

No cambia proveedores, NVS, navegacion, provisioning ni OTA. El renderer reinicia su estado de loading al entrar en una escena normal para poder reconstruir correctamente la pantalla si se vuelve a mostrar durante una recuperacion de red.

## Pruebas

- El renderer incluye el imagotipo anterior.
- El ancho revelado depende del porcentaje real.
- Existe conversion a gris y transicion a color.
- La barra V2 anterior no permanece en el bloque de loading.
- Los entornos normal y V2 real deben compilar.

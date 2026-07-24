# Mockups Lemon Box V2

Tres pantallas determinísticas de 480 × 480 para validar la dirección visual antes de tocar firmware:

- `home`: objeto ambiental con Bitcoin y próximo momento de mercado.
- `tape`: pasarela compacta de índices, acciones y Pack IA.
- `news`: interrupción editorial que explica qué pasó y por qué importa.

## Render

Desde la raíz de `lemon-box`:

```powershell
& .\docs\v2\mockups\render.ps1
```

El script usa Chrome local, no requiere red y genera:

- `rendered/home-480x480.png`
- `rendered/tape-480x480.png`
- `rendered/news-480x480.png`

Verificación reproducible:

```powershell
python .\docs\v2\mockups\verify_mockups.py
```

Los datos son ficticios y están marcados como demo. Los archivos oficiales de marca se consumen sin modificación desde `../assets/brand-official`.

## Decisiones deliberadas

- Todo contenido queda dentro de un inset de 48 px: 80% central del canvas.
- El imagotipo horizontal mide 120 px, el mínimo indicado para pantalla.
- Cada composición usa Black y una sola familia de acento.
- Alzas y bajas se distinguen con flechas y signos, no con un rojo inventado.
- No hay recomendaciones, targets ni botones de compra/venta.

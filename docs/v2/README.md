# Lemon Box V2

Documentación de producto para continuar Lemon Box sobre el hardware existente.

Estado: borrador de trabajo, 22 de julio de 2026.

Estos documentos capturan las decisiones conversadas hasta ahora. No autorizan deploys, actualizaciones masivas, compras, integraciones privadas ni publicación externa.

## Documentos

- [Dirección de producto](./2026-07-22-product-direction.md): tesis, principios, públicos, alcance y prioridades.
- [Especificación de experiencias](./2026-07-22-experience-spec.md): home ambiental, momentos, noticias, Market Tape, Packs e interacción.
- [Matriz de paridad V1/V2](./2026-07-22-v1-v2-feature-parity.md): infraestructura recuperada, funciones pendientes y limites no transaccionales.
- [Control, contenido y flota](./2026-07-22-control-plane.md): qué control remoto hace falta y cómo evitar construir un backoffice innecesario.
- [Cumplimiento de marca](./2026-07-22-brand-compliance.md): fuentes oficiales verificadas, diferencias con la implementación actual y reglas antes de diseñar.
- [Storyboard de demo física](./2026-07-22-demo-storyboard.md): demo determinística de 78 segundos sobre una sola unidad.
- [Mockups 480 × 480](./mockups/README.md): home ambiental, Market Tape y noticia importante, con render local repetible.
- [Assets oficiales y hashes](./assets/brand-official/SOURCES.md): logos y fuentes descargados sin modificación desde el Press Kit público.
- [Checklist de canary físico](./2026-07-22-physical-canary-checklist.md): detección USB, flash seguro de una unidad, validación visual y rollback.

## Resumen ejecutivo

Lemon Box V2 no será un producto desconectado ni un mini terminal profesional. Será una evolución de las aproximadamente 30 unidades existentes hacia un objeto financiero vivo para el escritorio.

La experiencia debe ser valiosa sin interacción: hora, Bitcoin y señales relevantes. En momentos puntuales puede cobrar vida con la apertura o el cierre del mercado, una noticia importante, un movimiento significativo o un hito. Al tocarla, el usuario obtiene contexto y puede continuar en Lemon.

La prioridad inmediata no es fabricar más hardware ni construir un dashboard grande. Es demostrar, sobre una unidad existente, que el software puede seleccionar mejor qué merece ocupar la pantalla.

## Restricciones vigentes

- Preservar y actualizar el hardware producido.
- No ejecutar operaciones ni emitir recomendaciones financieras personalizadas.
- No hacer push, deploy público ni actualización masiva sin autorización.
- Respetar el brandkit de Lemon sin reinterpretaciones arbitrarias.
- No tratar observaciones de la web como reglas de marca si no aparecen en el manual oficial.
- Diseñar para una pantalla de 480 x 480 y recursos acotados del ESP32-S3.

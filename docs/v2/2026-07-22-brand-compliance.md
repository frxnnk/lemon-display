# Lemon Box V2 — auditoría y cumplimiento de marca

Estado: auditado para prototipos internos. Fecha de corte: 22 de julio de 2026.

Este documento convierte las fuentes oficiales disponibles en reglas implementables para una pantalla de 480 × 480. No reemplaza una aprobación formal de Brand/Legal de Lemon antes de publicar o producir en escala.

## Principio rector

El brandkit no es inspiración: es una especificación. No se cambian colores, logo, proporciones, área de resguardo, tipografías o combinaciones por preferencia del equipo.

## Jerarquía de fuentes

Cuando dos fuentes difieren, se aplica este orden:

1. [Media Kit vigente](https://lemon.me/media-kit), actualizado el 7 de octubre de 2025.
2. Assets oficiales de la carpeta pública `Brand Guidelines / Logos nuevos`, creados en abril de 2026.
3. `LemonGuidelines_v1.1 (SEP2024).pdf`, sólo para reglas no contradichas por las capas anteriores.
4. [Sitio vigente de Acciones](https://lemon.me/acciones), sólo como evidencia de aplicación, nunca como permiso para inventar tokens.

Los archivos descargados sin alteración y sus hashes están documentados en [assets/brand-official/SOURCES.md](./assets/brand-official/SOURCES.md).

## Tokens vigentes de interfaz

| Token | RGB888 oficial | RGB565 teórico más cercano | RGB888 reconstruido |
|---|---:|---:|---:|
| Main Green | `#00DF1A` | `0x06E3` | `#00DF19` |
| Black | `#121212` | `0x1082` | `#101010` |
| Lime Yellow | `#CFFF2E` | `0xCFE6` | `#CEFF31` |
| Leaf Green | `#003D1B` | `0x01E3` | `#003D19` |

La tabla RGB565 usa redondeo al nivel representable más cercano. Antes de considerarla de producción hay que confirmar cómo convierte exactamente LVGL en el firmware y validar los cuatro tonos en la pantalla física.

### Colores propios del asset de logo

Los assets oficiales nuevos no deben recolorearse:

- La versión `green2` contiene `#00DA00` dentro del SVG.
- La versión `light` contiene `rgb(231,231,233)`.
- La versión black usa negro propio del asset.

Que `#00DA00` difiera del Main Green `#00DF1A` no es un error a corregir. El logo conserva su color embebido; la interfaz usa los tokens del Media Kit.

### Paleta anterior

El PDF de 2024 registra Black `#000000`, Greent `#00F068`, Evergreent `#00A849`, Nebula `#806CF2`, Solar `#FF8700`, Starlight `#E7E7E7` y Moon `#5B5B5B`. Esos valores aparecen hoy en firmware y Studio, pero no deben trasladarse automáticamente a V2. Sólo se mantienen si Lemon confirma que siguen habilitados en el sistema vigente.

## Tipografía

La carpeta oficial de Fonts contiene:

- PP Neue Machina Plain Bold: tipografía primaria para titulares, cifras y momentos.
- Satoshi Regular y Bold: tipografía secundaria para cuerpo, labels y contexto.

El PDF también menciona PP Neue Machina Plain Regular, pero ese archivo no está en la carpeta pública vigente. Por eso los mockups usan únicamente el peso Bold disponible. Geist, observado en el CSS del sitio de Acciones, no se toma como fuente normativa.

Regla del PDF: interlínea entre el tamaño de la fuente y hasta cuatro puntos por encima. En 480 × 480 se prioriza legibilidad a un metro y se evita condensar información para simular un terminal profesional.

## Logo e isologo

- Usar el imagotipo horizontal como opción dominante.
- Usar el vertical sólo cuando la composición lo exija.
- Priorizar el imagotipo completo en productos oficiales, partners y sponsors.
- No usar el wordmark solo.
- El imagotipo horizontal no debe bajar de 120 px de ancho en pantalla.
- El isologo no debe bajar de 16 px y mantiene siempre su orientación de 45°.
- El área libre alrededor del imagotipo equivale a un isologo completo por cada lado.
- No rotar, reflejar, recortar, distorsionar, separar, agregar sombras, contornos o formas, ni reemplazar tipografía.
- Elegir el archivo light sobre fondo oscuro y black sobre fondo claro.
- Usar isologo solo cuando el contexto ya identifique inequívocamente a Lemon; ante la duda, usar imagotipo.

Los tres mockups usan el imagotipo horizontal oficial a exactamente 120 px y no alteran el SVG.

## Composición

- Mantener el contenido principal dentro del 80% central de la composición. En 480 × 480 se implementa como un inset de 48 px por lado.
- Los fondos pueden ocupar la pantalla completa; logo, títulos, cifras y CTA quedan dentro del área segura.
- Evitar contenedores completamente rectos. El radio permitido es entre 8% y 16% del lado menor del elemento.
- En una composición oscura, usar una sola familia de luz/acento; no mezclar luces de colores diferentes.
- El manual 2024 permite transparencias tipo vidrio sólo con Starlight al 5%, blur de 16 a 48 y contorno diagonal de 45° y 3 pt. No se usan en los mockups iniciales.
- El glow debe ser moderado y funcional. No se usa en los mockups iniciales.
- Lucide es la librería principal de iconos. No se agregan iconos decorativos sin necesidad.
- Las rotaciones arbitrarias quedan reservadas al sistema de stickers; no se aplican a la UI del dispositivo.

## Movimiento

- Curva general: arranque ágil con 80% de aceleración y cierre suave con 20% de desaceleración.
- Evitar entradas lineales y bloques de texto monolíticos; revelar por máscara.
- El efecto voxel está permitido para construir o descomponer elementos.
- El glow puede remarcar entrada/salida, pero no dominar la escena.

En V2 se propone una única transición voxel corta para el cambio de apertura. Las pantallas de uso continuo deben permanecer calmas.

## Mercado, noticias y accesibilidad

El brandkit no define semántica bursátil para alzas y bajas. No se inventa un rojo de mercado. La dirección se comunica con flecha, signo y porcentaje; el color sigue siendo de marca.

Para datos de demo:

- Mostrar `DEMO` o `DATOS DE DEMO`.
- No presentar recomendaciones, targets ni lenguaje de compra/venta.
- Separar “qué pasó” de “por qué importa”.
- Citar fuente y hora cuando el contenido pase a datos reales.
- Usar nombres y composición de Packs sólo desde una fuente oficial de Lemon/Quaestus.

## Resolución de contradicciones

| Conflicto | Resolución aplicada |
|---|---|
| Verde del asset `#00DA00` vs Main Green `#00DF1A` | No recolorear el logo; usar `#00DF1A` sólo en UI. |
| Paleta 2024 vs Media Kit vigente | Media Kit manda; los colores antiguos quedan fuera hasta aprobación. |
| Geist en la web vs fonts oficiales | PP Neue Machina + Satoshi mandan. |
| PDF 2024 desalienta fondos vibrantes sólidos; sitio actual los usa | Para prototipo se admite Lime sólido porque aparece en la aplicación vigente; confirmar con Brand antes de producción. |
| Sitio usa recursos que no figuran como tokens | Tratar como observación, no como permiso reutilizable. |

## Matriz de cumplimiento de los mockups

| Regla | Home | Market Tape | Noticia |
|---|---:|---:|---:|
| 480 × 480 exacto | Sí | Sí | Sí |
| Área segura de 48 px | Sí | Sí | Sí |
| Logo oficial sin modificar, 120 px | Light | Light | Black |
| Tipografías oficiales descargadas | Sí | Sí | Sí |
| Paleta de UI vigente | Black + Main Green | Black + Lime Yellow | Lime Yellow + Black |
| Una sola familia de acento | Sí | Sí | Sí |
| Radio entre 8% y 16% | Sí | Sí | Sí |
| Sin rojo financiero inventado | Sí | Sí | Sí |
| Datos marcados como demo | Sí | Sí | Sí |
| Sin recomendación financiera | Sí | Sí | Sí |

## Pendientes antes de firmware

1. Aprobación de Brand sobre el uso de Lime sólido y neutrales en pantalla.
2. Confirmación Legal/Product del patrón de noticias y disclaimer.
3. Conversión RGB565 con la ruta real de LVGL y prueba física.
4. Verificación óptica de legibilidad, ghosting, brillo y temperatura de color.
5. Autorización explícita antes de reemplazar assets o tokens en firmware/Studio.

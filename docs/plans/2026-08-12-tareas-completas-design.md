# Tareas completas — diseño

## Objetivo

Convertir la lista mínima actual en un gestor de tareas local y autónomo que se use cómodamente desde el teléfono y desde la pantalla táctil de 480×480. Debe conservar la privacidad y seguir funcionando sin VPS ni internet, salvo por la sincronización horaria NTP ya existente.

## Enfoques considerados

1. **Modelo enriquecido en el ESP32 (elegido).** El aparato conserva tareas, subtareas, descripción, fecha y recordatorios; la web y la pantalla son dos vistas del mismo estado. Permite avisar aunque el teléfono esté cerrado y mantiene el sistema local.
2. **Metadatos sólo en el navegador.** Es más barato en firmware, pero cada teléfono tendría un estado diferente y los recordatorios no serían confiables con la página cerrada.
3. **Backend remoto.** Facilitaría sincronización fuera de casa y árboles ilimitados, pero rompe el requisito de privacidad/offline y agrega una dependencia innecesaria.

## Modelo de datos

Cada tarea principal tendrá:

- identificador estable;
- título completo;
- descripción opcional;
- estado completado;
- fecha/hora de vencimiento opcional;
- fecha/hora de recordatorio opcional;
- estado del recordatorio (pendiente, disparado o pospuesto);
- orden explícito;
- hasta un nivel de subtareas.

Cada subtarea tendrá identificador, título, estado y orden. No habrá subtareas dentro de subtareas. Cuando se complete la última subtarea pendiente, la principal se completará automáticamente; reabrir una subtarea reabrirá la principal. Completar la principal marcará todas sus subtareas como hechas.

La persistencia pasará a una versión nueva y hará una migración automática desde el formato actual, conservando títulos y estados. Se usarán identificadores en la API en lugar de índices para que reordenar o refrescar la página no opere sobre el elemento equivocado.

## Experiencia en la caja

### Lista

La pantalla principal mostrará tareas en tarjetas compactas de altura variable. El título envolverá líneas y no usará puntos suspensivos. Las tareas con subtareas tendrán un control para expandir/colapsar y un progreso `2/4`. La fecha aparecerá como metadato breve, con estado visual para hoy, vencida o próxima.

Como no caben todas las filas si el texto envuelve, la lista tendrá desplazamiento vertical táctil. El gesto horizontal seguirá cambiando de app y el gesto hacia arriba seguirá abriendo el selector. Los toques se separarán por zona: casilla para completar, chevron para expandir y cuerpo para abrir el detalle.

### Detalle

Al tocar el cuerpo se abrirá una vista dedicada con:

- título completo en varias líneas;
- descripción completa desplazable;
- fecha y recordatorio;
- lista de subtareas;
- acciones completar, posponer recordatorio y volver.

La vista compondrá sobre el sprite y usará el mismo chasis, paleta, tipografía y cortina de Ferced. No repintará la pantalla completa de manera continua.

### Recordatorio

El loop evaluará recordatorios sólo cuando el reloj NTP sea válido. Al vencer uno, creará una alerta local de prioridad alta, emitirá un sonido corto con el audio existente y mostrará una tarjeta sobre cualquier app. Tendrá acciones **Completar**, **Posponer 10 min** y **Abrir tarea**. Cada vencimiento se disparará una sola vez; reiniciar el aparato no repetirá una alerta ya atendida.

## Experiencia web móvil

La página seguirá embebida en PROGMEM, sin CDN ni fuentes remotas. El rediseño tendrá:

- cabecera con pendientes, vencidas y progreso;
- creación rápida;
- formulario de edición para título, descripción, vencimiento y recordatorio;
- subtareas agregables, editables, reordenables y colapsables;
- edición de tareas existentes;
- reordenamiento de tareas;
- filtros `Pendientes`, `Hoy`, `Todas` y `Hechas`;
- estados de carga, guardado, error y lista vacía;
- confirmación/deshacer para borrados destructivos;
- controles accesibles con nombres y áreas táctiles de al menos 44 px.

La web refrescará por revisión y preservará el formulario abierto para que el sondeo periódico no borre lo que el usuario está escribiendo.

## API y seguridad

Se mantendrá la defensa CSRF mediante `X-Ferced`. La API expondrá un documento versionado y operaciones por ID para crear, editar, completar, borrar y reordenar tareas y subtareas. Las entradas se validarán y normalizarán en el firmware; los errores devolverán JSON consistente y la web no aplicará cambios optimistas que no pueda revertir.

## Límites

Se fijarán límites explícitos medidos contra RAM, NVS y tamaño de respuesta. El objetivo inicial es hasta 24 tareas principales, 8 subtareas por tarea, títulos de 120 bytes y descripciones de 512 bytes, ajustables después de medir el binario y la memoria. No se implementan recurrencia, sincronización cloud ni notificaciones push del navegador en esta versión.

## Verificación

- Tests de almacenamiento: migración, serialización, límites, IDs, cascada de completado y posposición.
- Tests de API: validación, errores, edición concurrente y defensa CSRF.
- Simulador: lista vacía, títulos largos, tareas vencidas, expansión, scroll, detalle y tarjeta de recordatorio.
- Página extraída: pruebas de interacciones y capturas en anchos móviles y escritorio.
- Build `ferced_display` y `ferced_display_vps`, medición de flash/RAM y revisión visual de capturas.
- Prueba en hardware del tacto, audio, NTP, persistencia tras reinicio y recordatorio real.


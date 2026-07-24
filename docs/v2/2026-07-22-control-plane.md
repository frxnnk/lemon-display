# Lemon Box V2 - Control, contenido y flota

Estado: alcance conceptual.

## Decisión

No hace falta construir ahora un gran dashboard. Sí hace falta una capa de control si Lemon va a operar aproximadamente 30 cajas sin configurarlas una por una.

El valor comercial está en lo que ocurre en la cajita, no en crear otro backoffice complejo.

## Tres superficies diferentes

### 1. Configuración personal

Para el dueño de una caja:

- Activo principal.
- Watchlist.
- Packs seguidos.
- Noticias activas.
- Sonido y horarios silenciosos.
- Densidad de contenido.

Idealmente vive dentro de Lemon o en una mini web abierta desde un QR. No requiere un dashboard operativo.

### 2. Operación de flota

Para Lemon y el equipo que administra los dispositivos:

- Estado online y última conexión.
- Versión de firmware.
- Salud básica y uptime.
- Rol: oficina, prensa, embajador, CEO o laboratorio.
- Perfil y configuración asignados.
- Canal de actualización.
- Canary, despliegue gradual y rollback.

La primera versión puede ser una tabla simple con detalle lateral. No necesita una navegación compleja ni analítica sofisticada.

### 3. Control editorial

Para crear y publicar momentos:

- Plantilla: noticia, apertura, cierre, hito o campaña.
- Titular y explicación.
- Activos relacionados.
- Audiencia o segmento.
- Inicio, vencimiento y prioridad.
- Preview exacto de 480 x 480.
- Publicar, retirar y auditar.
- Deep link atribuible.

Las plantillas cerradas reducen errores y ayudan a cumplir marca, legales y límites de pantalla.

## Alcance gradual

### Demo

- Consola local.
- Una caja.
- Fixtures determinísticos.
- Disparo manual de escenas.
- Preview y reproducción de una secuencia.

### Piloto

- Entre 5 y 10 dispositivos.
- Registro de dispositivos y pairing.
- Estado online, versión y rol.
- Configuración remota acotada.
- Publicación de tarjetas con vencimiento.
- Canary OTA y rollback validado físicamente.
- Métricas de uptime, interacción y scans.

### Escala

- Plan de actualización de las 30 unidades.
- Segmentos y campañas.
- Permisos editoriales.
- Scheduling.
- Auditoría de publicaciones.
- Comparación por roles.
- Decisión de escala a 30-100 unidades.

## Reutilización del proyecto existente

No se debe comenzar un panel desde cero sin revisar y reutilizar:

- Studio y su preview.
- Pairing y tokens.
- APIs locales de health, settings y watchlist.
- Jobs de build, flash y OTA.
- Modelo cloud de desired/reported state.

Actualmente estas piezas no forman un control remoto completo. Studio respalda configuraciones concretas, pero parte de las experiencias arbitrarias funciona sólo como preview. El firmware tampoco está conectado integralmente al protocolo cloud existente.

## Flujo objetivo

```text
Editor / operador
        |
        v
Control de contenido y flota
        |
        v
Estado deseado + tarjetas firmadas/versionadas
        |
        v
Lemon Box -> estado reportado + métricas mínimas
        |
        v
QR / deep link -> Lemon
```

## Métricas mínimas

- Última conexión.
- Uptime y reinicios.
- Versión de firmware.
- Éxito o fallo de sincronización.
- Tarjeta recibida y mostrada.
- Interacción táctil agregada.
- QR generado y scans atribuibles.

No se deben recopilar datos personales o telemetría innecesaria para demostrar utilidad.

## Seguridad y rollout

- Dispositivos identificados individualmente.
- Contenido con versión, expiración e integridad verificable.
- OTA firmada.
- Canary de una unidad conocida antes de ampliar.
- Observación mínima antes de cada anillo.
- Rollback automático o procedimiento físico documentado.
- Sin actualización masiva sin autorización explícita.

## Criterio de éxito

El control plane es suficiente cuando permite operar el piloto con seguridad y responder estas preguntas:

- ¿Qué está mostrando cada perfil?
- ¿Qué cajas están sanas y actualizadas?
- ¿Podemos retirar contenido incorrecto rápidamente?
- ¿Podemos actualizar una unidad antes que el resto?
- ¿Las personas miran, tocan o continúan en Lemon?


# OTA por WiFi — diseño

Fecha: 2026-08-04. Firmware ferced-display, ESP32-S3.

## Qué resuelve

Hoy la única forma de actualizar el aparato es por USB. El 2026-08-04 eso costó
seis combinaciones de cable y puerto antes de funcionar, y el diagnóstico
terminó siendo que el contacto es marginal. El WiFi, en cambio, no falló una
sola vez en toda la sesión.

Que actualizar dependa del conector más frágil del aparato es la fragilidad real
del proyecto.

**Ojo con la expectativa:** habilitar el OTA requiere un flasheo por USB. No
sirve para salir de un apuro, sirve para que no haya un próximo apuro.

## Las tres verificaciones que lo habilitan

Se comprobaron antes de diseñar nada, porque cualquiera de las tres podía
matarlo:

| Qué | Resultado |
|---|---|
| ¿Hay dos particiones de app? | Sí. `default_16MB.csv` trae `app0` (ota_0) y `app1` (ota_1) de 6,5 MB, más `otadata`. No hay que reparticionar — que también habría pedido USB. |
| ¿Se puede reusar `ota_manager.cpp`? | Sólo la idea. Su API está armada alrededor de GitHub (`otaCheck(repo)`, `otaCheckAsset`) y arrastra `ROOT_CAS` desde `api_client.cpp`, que está excluido del build de Ferced. |
| ¿Hay rollback si el firmware nuevo arranca mal? | Sí. `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` en el sdkconfig del framework, con la API en `esp_ota_ops.h`. |

## Decisiones acordadas

**El binario baja del proxy**, no de GitHub ni de un servidor en el aparato. El
proxy ya está autenticado con Bearer token, ya tiene TLS por Caddy y ya tiene
rate limit: no se agrega ninguna superficie de ataque nueva. Se descartó que el
aparato levante su propio servidor de subida justamente porque hoy el aparato
**no expone nada**, sólo consume, y esa propiedad vale la pena conservarla.

**El disparo es manual**, con un cuarto botón en la pantalla de configuración.
Nada se actualiza a tus espaldas, y si falla estás mirando la pantalla en ese
momento. El chequeo automático queda para cuando el mecanismo tenga kilómetros.

**Se valida el certificado del servidor.** `feed_client.cpp` usa
`setInsecure()`, que cifra pero no valida: para noticias públicas es un
intercambio razonable, para un binario ejecutable no. Sin validar, alguien en el
camino podría servir su propio firmware, y eso es ejecutar código arbitrario en
el aparato. Se embebe el certificado raíz de Let's Encrypt (ISRG Root X1, vence
en 2035) y se valida de verdad en la descarga. El feed puede seguir como está.

Se evaluó firmar el binario, que protegería incluso si el VPS se comprometiera.
Se descartó por ahora: pide manejo de claves y un paso de firma en el build. Si
la Lemon Box alguna vez se vende, ahí sí.

## Arquitectura

**Proxy**, dos endpoints detrás del guard que ya existe:

| Endpoint | Devuelve |
|---|---|
| `GET /v1/firmware` | JSON con versión, tamaño y hash del binario |
| `GET /v1/firmware/bin` | El `.bin` crudo |

El binario se sube al VPS por `scp`, igual que el propio proxy.

**Aparato**, `src/ota_ferced.{h,cpp}`. Módulo nuevo y chico:

1. Pide `/v1/firmware` y compara la versión con `FERCED_VERSION`.
2. Si hay una nueva, streamea `/v1/firmware/bin` a la partición inactiva con
   `Update.h`, informando porcentaje.
3. Reinicia.

Se habla con el proxy igual que `feed_client.cpp`: `WiFiClientSecure` con Bearer
token, pero **con CA real en vez de `setInsecure()`**.

## La red de seguridad

Con `ROLLBACK_ENABLE`, el firmware recién instalado arranca en estado
`PENDING_VERIFY`. Si no confirma que está sano, el próximo reinicio vuelve solo
a la partición anterior.

La confirmación no va al arrancar, va **cuando el firmware demuestra que
sirve**: WiFi conectado y un feed traído con éxito. Recién ahí se llama a
`esp_ota_mark_app_valid_cancel_rollback()`. Un firmware que compila y bootea
pero no consigue red se revierte solo, que es exactamente el caso peligroso.

**A verificar en la implementación:** si Arduino-ESP32 marca la app como válida
automáticamente al arrancar, el rollback queda anulado y hay que diferirlo. Es
lo primero que hay que comprobar antes de confiar en esta red.

## Verificación

El camino feliz se prueba de punta a punta: subir un binario con versión mayor,
tocar el botón, ver el progreso y que el aparato vuelva con la versión nueva en
la pantalla de configuración.

El rollback se prueba a propósito: subir un binario que arranque y **no
consiga** red, confirmar que el aparato vuelve solo al anterior. Sin esa prueba
la red de seguridad es una suposición.

## Lo que no hace

- No chequea solo: el disparo es manual.
- No firma el binario.
- No sirve para recuperar un aparato que ya no arranca: para eso está el USB.

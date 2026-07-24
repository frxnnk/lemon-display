# Operacion OTA para Lemon Box V2

## Contrato del canal

- Repositorio: `frxnnk/lemon-display`.
- Version local/publica: `5.1.1-beta.48`.
- Asset V2 exacto: `firmware-v2.bin`.
- Asset normal preservado: `firmware.bin`.
- Frecuencia: boot, probe de etiqueta cada 60 segundos y chequeo completo cada seis horas como fallback.
- Integridad obligatoria V2: MD5 asociado al nombre del asset en el body.
- Accion en pantalla: aviso en Home, primer tap para armar durante ocho segundos y segundo tap para confirmar e instalar.

Una release normal sin `firmware-v2.bin`, un asset con otro nombre o una release sin el MD5 especifico no se ofrece a V2.

## Publicacion de una version siguiente

1. Incrementar `APP_VERSION` y compilar/verificar los canales que se quieran publicar.
2. Probar `firmware-v2.bin` por USB en una canary con rollback guardado.
3. Calcular MD5 y SHA-256 del binario final, no de una build anterior.
4. Crear o editar una release mas nueva y adjuntar el asset con nombre exacto.
5. Incluir una linea `firmware-v2.bin MD5: <hash>` en el body.
6. Probar el aviso y la instalacion OTA primero en la canary controlada.
7. Recién despues dejar la release disponible para las cajas entregadas.

No se hizo push de codigo. La release OTA V2 si es publica y deliberadamente no contiene `firmware.bin`.

## Evidencia de build preparada

| Artefacto | Bytes | SHA-256 |
|---|---:|---|
| normal local `firmware-normal.bin` | 1.452.464 | `e0cee8f5c647ad012846feaf80f5b09395cc12003cbe32d50fcbd6eb2769d915` |
| V2 publico `firmware-v2.bin` | 1.380.464 | `5b79f188627b355a3e5b8ff3b4f1ee0bb71617d9b2e7fa950015106577285db3` |

MD5 V2 de esta build: `900038bd91265d59b3ba3d302cf2aedb`.

## OTA V2 beta.43

- Asset: `output/v2-delivery-20260722-beta43/firmware-v2.bin`
- Bytes: `1.351.408`
- SHA-256: `1768d5962e0e2b231910c775b32d9ed1b44992e483cbbb79d290ed6164e571c7`
- MD5: `5cdc9b9e6f4016c493781fe91160f2d0`
- Alcance: recuperacion de Wi-Fi y validacion antes de persistir credenciales.

## OTA V2 beta.44

- Asset: `output/v2-delivery-20260722-beta44/firmware-v2.bin`
- Bytes: `1.379.104`
- SHA-256: `2fc38179c5814f54c0b44235c8f095a8ec1987a1f7f50dd090c73fdfb16b7988`
- MD5: `8535ba1ea46e62f500de10a62b165166`
- Alcance: loading V2 con imagotipo anterior y llenado gris a verde ligado al progreso real.

## OTA V2 beta.45

- Asset: `output/v2-delivery-20260722-beta45/firmware-v2.bin`
- Bytes: `1.379.904`
- SHA-256: `f353eab366b77e81eb4ac528faad5450760da5d0245e494c4f6ae787b57de549`
- MD5: `e6586ce8389dec910a3659907c98e3aa`
- Alcance: limpieza visual, navegacion directa y confirmacion OTA desde Home.

## OTA V2 beta.46

- Asset: `output/v2-delivery-20260722-beta46/firmware-v2.bin`
- Bytes: `1.380.080`
- SHA-256: `7528eb5fe83995d4771bed6daa150deae842992dcfba51efb8a9d8fc9720cfdf`
- MD5: `6b3acde3481c02c0729929656bb539fb`
- Alcance: doble clip RGB, reloj deduplicado y BTC/USD con precision visible de ticks.

## OTA V2 beta.47

- Asset: `output/v2-delivery-20260723-beta47/firmware-v2.bin`
- Bytes: `1.380.928`
- SHA-256: `dc9a99bd9ff9efd4be3ed2ec00fc7bc15c49265d959d8025277b5461e4c6b513`
- MD5: `505f03aabe4562a024eb887b5d21f15f`
- Alcance: hero reordenado y deteccion liviana de nuevas OTA en aproximadamente 60 segundos.

## OTA V2 beta.48

- Asset: `output/v2-delivery-20260723-beta48/firmware-v2.bin`
- Bytes: `1.380.464`
- SHA-256: `5b79f188627b355a3e5b8ff3b4f1ee0bb71617d9b2e7fa950015106577285db3`
- MD5: `900038bd91265d59b3ba3d302cf2aedb`
- Alcance: alineacion visual real del precio y boton de Ajustes inferior independiente.

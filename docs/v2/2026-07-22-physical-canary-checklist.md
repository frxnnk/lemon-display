# Lemon Box V2 real - checklist fisico de canary y entrega

Estado: software verificado; flash fisico final pendiente de validacion visual/touch.

## Artefacto actual

- Entorno: `matouch_esp32s3_40_v2_real`.
- Version: `5.1.1-beta.48`.
- Binario OTA/entrega: `output/v2-delivery-20260723-beta48/firmware-v2.bin`.
- Tamano: 1.380.464 bytes.
- SHA-256: `5b79f188627b355a3e5b8ff3b4f1ee0bb71617d9b2e7fa950015106577285db3`.
- MD5 para body de release: `900038bd91265d59b3ba3d302cf2aedb`.
- Particiones de referencia SHA-256: `BD0F7954ACA2EF7D925EE21AAA1F3DC8822D1D6CE5CBBD26A135E5886BFFF6CE`.
- Suite: 157 tests OK.
- Builds verificados en esta version: normal y V2 real OK.

Los hashes son de esta build exacta; recalcular ante cualquier cambio.

## Regla por unidad

1. Conectar una sola caja por USB de datos, sin hub.
2. Detectar puerto, VID/PID y serial en esa sesion. La MaTouch valida VID `0x303A`; no asumir COM7.
3. Registrar ID fisico, serial/MAC y destino de entrega.
4. Ejecutar el helper con puerto y serial exactos. El helper compara particiones y lee un rollback individual antes de escribir.
5. Escribir solo app en `0x10000`, `--flash-mode keep --flash-size keep`. Nunca `erase-flash`.
6. Validar la caja y desconectarla antes de conectar la siguiente.

Si Windows no puede hacer auto-reset, entrar manualmente al bootloader: desconectar, mantener BOOT, conectar, esperar dos segundos y soltar. Luego usar `-ManualBootloader`.

```powershell
& .\tools\flash_v2_real_canary.ps1 -Port COMX -ExpectedSerial 'SERIAL-DETECTADO' -ConfirmCanary -ManualBootloader
```

## Validacion visual y tactil

- Loading aparece inmediatamente y muestra fases; QR aparece solo si no hay Wi-Fi guardado.
- El imagotipo completo comienza gris y se llena en verde de izquierda a derecha; no aparece la barra V2 anterior.
- Si la red guardada falla, aparece `CONFIGURAR OTRA RED`; el toque abre el QR y una red nueva se persiste solo despues de conectar.
- Home queda persistente sin rotulo `HOME`, con hora, BTC/par seleccionado, sparkline sin linea base, Dolar Lemon Compra/Venta y stock enfocado.
- Precio queda centrado visualmente contra el grafico; Ajustes aparece como boton contorneado con sliders junto a Market Tape.
- Dejar al menos 90 segundos: reloj, WS, Dolar y stocks refrescan sin salto de frame completo.
- En BTC/USD, verificar que los centavos cambien y que freshness permanezca `LIVE`; `CACHED` indica que el WS no esta recibiendo.
- Tap en hero recorre BTC/USD, BTC/ETH, BTC/SOL, BTC/ARS y BTC/ORO.
- Market Tape abre; una fila abre Contexto; chevron, titulo y swipe vuelven al nivel anterior.
- El engranaje abre Settings; tocar los titulos cambia entre Pantalla, Datos y Dispositivo.
- Dispositivo muestra firmware, diagnostico y `AL DIA` mientras no exista asset V2 nuevo.
- Wi-Fi, IP de Studio y freshness (`LIVE`, `CACHED`, `STALE`, `OFFLINE`) son coherentes.

## OTA

El canal publico actual es `v5.1.1-beta.48` y contiene exclusivamente `firmware-v2.bin` y `firmware-v2.sha256`; el firmware historico no toma esos assets.

Al publicar una version futura, adjuntar el archivo con nombre exacto `firmware-v2.bin` y agregar en el body:

```text
firmware-v2.bin MD5: 900038bd91265d59b3ba3d302cf2aedb
```

La prueba OTA de beta.48 requiere una caja V2 en beta.47: verificar que el aviso aparezca en aproximadamente 60 segundos sin reiniciar, confirmar con dos toques y validar alineacion, boton Ajustes, WS y bounce. No instalar simultaneamente en todas las unidades antes de validar una.

## Rollback

Cada ejecucion crea `output/v2-real-canary-<timestamp>/rollback-app.bin` y `ROLLBACK_COMMAND.txt`. Conservar la carpeta asociada al serial de esa caja. Si el flash normal se interrumpe, no repetir a ciegas: seguir recovery de `lemon-box-flasher`. No borrar NVS ni la tabla de particiones.

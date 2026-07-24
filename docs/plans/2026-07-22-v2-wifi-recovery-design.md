# Lemon Box V2: recuperación de Wi-Fi

## Objetivo

Una caja que arranca fuera de la red guardada debe poder configurarse en otra red sin cable, reset de fábrica ni acceso previo a Studio.

## Flujo elegido

1. Si no hay credenciales guardadas, V2 abre el QR de provisioning como antes.
2. Si hay credenciales, intenta conectarse durante la ventana existente de 15 segundos.
3. Si conecta, continúa con NTP, proveedores y home.
4. Si falla, muestra una escena V2 estable con el SSID fallido y la acción `CONFIGURAR OTRA RED`.
5. Mientras esa escena está visible, el firmware sigue reintentando la red anterior. Si reaparece, entra al home automáticamente.
6. Al tocar la acción, abre el AP y el QR/captive portal existentes.
7. Las credenciales recibidas se prueban primero. Sólo se escriben en NVS después de una conexión exitosa.
8. Si la prueba falla, vuelve a la escena de recuperación y permite reintentar sin borrar la configuración anterior.

## Decisiones de seguridad

- No abrir un access point por un corte transitorio sin mostrar antes el estado al usuario.
- No borrar la red guardada al fallar un intento.
- No persistir una contraseña incorrecta.
- Conservar el doble long press de `Ajustes > Datos > Wi-Fi` como recuperación manual.
- Mantener el canal OTA V2 aislado mediante el asset exacto `firmware-v2.bin` y MD5 específico.

## Pruebas

- Una red guardada que falla ofrece recuperación y no borra NVS.
- Las credenciales nuevas se guardan después de `wifiConnected()`.
- La escena tiene una acción touch explícita que abre provisioning.
- El firmware conserva carga inicial, navegación y canal OTA V2.


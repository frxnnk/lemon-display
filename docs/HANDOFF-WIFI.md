# ferced-display — traspaso: la red se cae

Estado al 2026-08-07, firmware **1.4.6**. Rama `feat/ferced-display`.

Este documento es para quien venga a averiguar **por qué el aparato se queda sin
red cada tanto**. La pregunta que hay que contestar es «¿el receptor o el
router?».

**Actualización 2026-08-07 (más tarde): las tres instrumentaciones del punto 3
y los dos arreglos del punto 4 ya están en la 1.4.6**, flasheada y verificada
por serie (y publicada en el canal OTA del VPS). O sea:

- `[Boot] motivo=N (nombre)` sale al arrancar. Ojo: un reset por esptool da
  `0 (desconocido)` — esa es su firma normal, no un misterio. Lo que discrimina
  es un 6 (brownout), un 4 (panic) o un 3 (software) en un reinicio espontáneo.
- `[WiFi] caida: reason=N rssi=N t=N` sale en cada desconexión. El RSSI es el
  muestreado hasta 5 s antes de la caída (en el momento de la caída ya no hay
  enlace que medir). La tabla del punto 3b es la clave de lectura.
- La señal en dBm está en la pantalla de configuración, bloque de red, fila
  SEÑAL.
- `WiFi.setSleep(false)` puesto y el tope del backoff bajado de 5 min a 60 s.

**Lo que falta es sólo el paso 3 del plan: dejarlo capturando y leer la tabla.**
Hay un capturador con hora de pared por línea en `tools/capture_serie.ps1`
(escribe en `output/serie-*.log`; mientras corre retiene COM3, pararlo antes de
flashear). La hora de pared detecta reinicios aunque no se capture el arranque:
los millis del aparato van para atrás.

Leé el punto 2 antes de proponer una causa: hay cuatro sospechosos ya
descartados con evidencia, y volver sobre ellos es tiempo perdido.

---

## 1. Qué se observó

Todo esto es de capturas por serie del 2026-08-07, con el aparato en el
escritorio y conectado por USB a la PC.

**Falla el DNS apenas conecta:**

```
[WiFi] Connected! IP: 192.168.1.42
[WiFi] DNS override: ip=192.168.1.42 gw=192.168.1.1 dns=8.8.8.8/1.1.1.1
[ 19247][E][WiFiGeneric.cpp:1583] hostByName(): DNS Failed for feed.ferced.com
[Feed] HTTP -1; conservo cache
```

Ojo con esto: el firmware **ya fuerza** 8.8.8.8 y 1.1.1.1 (`applyPublicDns()` en
`wifi_manager.cpp`), así que el DNS del router está fuera de la ecuación. Falla
igual.

**Falla el TLS, intermitente, desde hace días:**

```
[ 29292][E][WiFiClientSecure.cpp:144] connect(): start_ssl_client: -1
[Feed] HTTP -1; conservo cache
```

Aparecía una o dos veces cada 100–150 s ya el 2026-08-05.

**Se cayó la asociación y volvió:**

```
[WiFi] Connection failed
[WiFi] Connecting
. . . . . . . . . . .
[WiFi] Connected! IP: 192.168.1.42
```

**Y hubo un reinicio espontáneo.** Se detectó porque los millis del log fueron
hacia atrás entre dos capturas (37487 → 19247). **No se capturó el motivo**, que
es justamente el dato que más falta.

**Durante los cortes el aparato no responde a ping ni a HTTP**, y vuelve solo al
cabo de un rato.

**No está en bucle de reinicio.** 95 segundos de captura continua, cero
reinicios, y la telemetría de animación normal todo el tiempo (`fps` 33–37,
`timeouts=0`). Cuando está conectado, funciona perfecto.

---

## 2. Qué ya está descartado

No vuelvas sobre esto sin evidencia nueva.

**No es la red entera ni Internet.** Durante los cortes del aparato, la PC —en
**la misma SSID y la misma banda**— siguió andando sin un hipo: `curl` a
`feed.ferced.com` y `ssh` al VPS corriendo continuamente. Sea lo que sea, es
específico de ese cliente o de cómo el AP lo trata a él.

**No es el DNS del router.** El firmware lo pisa con 8.8.8.8 / 1.1.1.1 apenas
asocia, y falla igual.

**No es el modo transición WPA2/WPA3**, que es el sospechoso clásico con ESP32.
El AP está en **WPA2-Personal / CCMP** puro, medido con `netsh wlan show
interfaces`.

**No es el código de OTA ni el de TLS del firmware.** El feed usa
`setInsecure()` y también falla, y el DNS falla *antes* de que haya un handshake.
Es de más abajo.

**No es un crash loop.** Ver arriba.

**Cuidado con culpar a la alimentación por USB.** Es tentador —el reinicio pasó
con el aparato enchufado a la PC, y un panel RGB de 480x480 más los picos de
transmisión son consumo real— pero **los cortes de descarga del OTA pasaron el
2026-08-06 con el aparato NO conectado a la PC**. O sea que la inestabilidad de
red existe en las dos configuraciones de alimentación. La alimentación puede
explicar el reinicio; no explica los cortes.

---

## 3. Lo que faltaba medir — **implementado en la 1.4.6, ver arriba**

Tres datos. Los tres están en la 1.4.6; queda esta sección porque las tablas de
lectura siguen siendo la clave del diagnóstico.

### a) El motivo del reinicio

Nada llama a `esp_reset_reason()` al arrancar. Brownout, panic y reinicio por
software son tres problemas completamente distintos y hoy son indistinguibles.

```cpp
// en setup(), antes de cualquier otra cosa
Serial.printf("[Boot] motivo=%d\n", (int)esp_reset_reason());
```

`ESP_RST_BROWNOUT` (=6) sería alimentación y cierra el caso. `ESP_RST_PANIC`
(=4) es un bug del firmware. `ESP_RST_SW` (=3) es el OTA o un reinicio pedido.

### b) El motivo de la desconexión

No hay manejador de eventos de WiFi, así que el `wifi_err_reason_t` que da el
SDK —el dato más discriminante de todos— se tira.

```cpp
WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    Serial.printf("[WiFi] caida: reason=%d rssi=%d\n",
                  info.wifi_sta_disconnected.reason, (int)WiFi.RSSI());
}, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
```

Cómo se lee:

| reason | Qué significa | Culpable |
|---|---|---|
| 200 `BEACON_TIMEOUT` | dejó de oír al AP | señal, distancia, interferencia |
| 201 `NO_AP_FOUND` | no lo encuentra al reasociar | señal, o *band steering* |
| 202 `AUTH_FAIL` / 204 `HANDSHAKE_TIMEOUT` | falló la autenticación | AP o clave |
| 2 `AUTH_EXPIRE` / 3 `ASSOC_EXPIRE` | **el AP lo echó** | router |
| 8 `ASSOC_LEAVE` | se fue solo | firmware |

**Esa tabla es la respuesta a la pregunta del título.** Un 200 o 201 apunta al
receptor y a dónde está puesto; un 2 o un 3 apunta al router.

### c) El RSSI

`wifiRSSI()` ya existe en `wifi_manager.cpp:201`, pero **en el build de Ferced no
lo mira nadie**: no se loguea y no está en la pantalla de configuración. Por
debajo de −75 dBm todo lo de arriba es esperable y no hay misterio que resolver.

Conviene en los dos lados: un renglón por serie junto a cada caída, y un dato
más en el bloque de RED de configuración (`ui_config.cpp`, al lado de SSID / IP
/ FEED — hay lugar en la columna).

### d) Y de paso, cuántos AP hay con esa SSID

No se pudo determinar: Windows no lista la red a la que ya está conectado. Si la
casa tiene repetidor o mesh, el ESP32 siendo empujado de un nodo a otro explica
todo el cuadro y es de los casos más comunes. El BSSID al que está asociada la
PC es `a0:8a:06:90:9e:bf`. Desde el aparato se puede contar con `WiFi.scanNetworks()`
—`wifiStartScan()` ya está implementado— o desde el teléfono con cualquier
analizador de WiFi.

---

## 4. Dos cosas para arreglar igual — **hechas en la 1.4.6**

Las dos eran de una línea y las dos empeoraban el problema.

### El ahorro de energía del WiFi está prendido

Nadie llama a `WiFi.setSleep(false)`, así que rige el default del ESP32, que es
*modem sleep* activado. Es una causa conocida de beacons perdidos, latencia a
saltos y justamente esta clase de inestabilidad. **Este aparato está enchufado a
la pared: no tiene ningún motivo para ahorrar energía.**

Va en `wifiConnect()`, en `wifi_manager.cpp`, después de asociar. Es el primer
cambio que yo probaría, antes que cualquier otro.

### El backoff convierte un bache en un corte largo

`wifiLoop()` duplica el intervalo de reintento: 10 s → 20 → 40 → … → **5
minutos**. O sea que si el WiFi vuelve a los diez segundos, el aparato puede
seguir desconectado cinco minutos porque le toca esperar. **Eso explica que "no
responde" durara tanto**, y es lo que más se nota como usuario.

Un backoff exponencial tiene sentido cuando reintentar cuesta —batería, o una
API que cobra—. Acá no cuesta nada. Un tope de 60 o 90 segundos es más sensato
para algo que vive enchufado.

---

## 5. El orden que yo seguiría

1. **Poner las tres instrumentaciones del punto 3** (a, b y c). Media hora, y
   sin eso todo lo demás es adivinar. Ya me equivoqué dos veces en este proyecto
   por proponer una causa antes de medir: una vez culpé al `WriteTimeout` del
   proxy —lo probé bajando el binario a 40 KB/s y entró entero— y otra a las
   raíces de Let's Encrypt.
2. **`WiFi.setSleep(false)` y bajar el tope del backoff.** Baratos, correctos
   por su cuenta, y pueden hacer desaparecer el síntoma.
3. **Dejarlo un día entero capturando** y leer la tabla de `reason`.
4. Recién ahí, con el número en la mano, tocar el router o mover el aparato.

**No cambies el router ni compres nada antes del paso 3.** El aparato anda
perfecto cuando está conectado; el problema es la conexión, y todavía no
sabemos de qué lado.

---

## 6. Contexto que conviene tener

- El aparato es un MaTouch ESP32-S3 4.0". WiFi **802.11 b/g/n, sólo 2,4 GHz**:
  no puede irse a 5 GHz aunque el router la tenga.
- El AP anuncia **802.11ax en 2,4 GHz, canal 11**. Un AP WiFi 6 con clientes
  legacy a veces se porta distinto; no está descartado ni confirmado.
- La red se llama `Valholl`. El aparato toma IP por DHCP y quedó en
  192.168.1.42; el gateway es 192.168.1.1.
- **El firmware tolera bien los cortes por diseño**: `feedFetch()` conserva el
  cache y la pantalla nunca se queda vacía (`FEED_STALE_CACHE`). Por eso esto se
  nota como "no puedo actualizar" o "no le llegan los avisos", y no como una
  pantalla rota.
- El corte de descarga del OTA que motivó todo esto **ya está mitigado** en el
  firmware: 1.4.2 dejó de abortar ante un `readBytes()` que devuelve cero, que
  es un bache y no un corte. Ver `docs/HANDOFF-UI.md`, punto 11. O sea que la
  red inestable ya no rompe la actualización, pero sigue rompiendo el resto.
- Para escuchar la serie hay un lector en el scratchpad de la sesión; cualquier
  monitor sirve, a 115200. Con `ARDUINO_USB_CDC_ON_BOOT=1` —que los entornos
  `ferced_display` y `ferced_display_vps` ponen— `Serial` sale por el USB nativo.
- **Desconfiá del visor, no sólo del aparato.** Perdí un rato creyendo que un
  acento llegaba roto porque la consola de Python en Windows es cp1252 y me lo
  mostraba mal. Los bytes crudos decían `c3 a9`, o sea UTF-8 perfecto. Cuando
  algo se ve raro en la serie, volcalo en hexadecimal antes de creerle.

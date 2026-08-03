# Pantalla de configuración — diseño

Fecha: 2026-08-03. Firmware ferced-display, ESP32-S3 480x480 táctil.

## Qué resuelve

Hoy no hay forma de saber qué firmware tiene puesto la cajita mirándola. El
problema apareció en la práctica: después de una tanda de cambios, la pantalla
se veía igual que siempre y no había manera de distinguir "no se flasheó" de
"se flasheó pero el cambio es sutil".

La pantalla también junta en un lugar los datos que hoy sólo se consiguen por
consola serie o leyendo el log del proxy.

## Alcance

Información más dos acciones. **No** es una pantalla de ajustes: no cambia ni
persiste ninguna configuración. Se descartó esa opción por YAGNI — brillo e
intervalo de rotación no se piden hoy, y meterlos exige controles táctiles, NVS
y que el resto del firmware lea esos valores.

## Arquitectura

Fase nueva `PHASE_CONFIG` en la máquina de estados de `ferced_main.cpp`, más un
módulo `src/ui_config.{h,cpp}`.

Dibuja **directo a `tft`**, sin sprite ni bandas sucias, igual que
`provisionDrawQR()`. Es una pantalla estática: no necesita la maquinaria de
animación.

La función de dibujo no consulta nada, recibe todo:

```cpp
struct ConfigInfo {
    const char* version;   // "1.0.0"
    const char* commit;    // "e96efeb" o "e96efeb-dirty"
    const char* built;     // "2026-08-03 23:51"
    const char* ssid;
    IPAddress   ip;
    const char* endpoint;  // solo el host
    uint32_t    uptimeS;
    float       fps;
    uint8_t     items;
    bool        online;
};
void uiConfigDraw(const ConfigInfo&);
```

**Esa separación es el punto del diseño, no un detalle de estilo.** Si la
función consultara WiFi o NVS adentro, no compilaría en el simulador, que sólo
tiene shims de gráficos. Recibiendo los datos se puede iterar la pantalla en la
PC en segundos, que es como se trabaja la UI en este proyecto.

`ferced_main.cpp` arma la struct desde WiFi, NVS y `uiFercedStats()`, maneja la
fase y ejecuta las acciones.

## Disposición

```
   ▌ FERCED                                    mark 2x + wordmark

   Versión        1.0.0
   Commit         e96efeb
   Compilado      2026-08-03 23:51
   ─────────────────────────────────
   Red            MiWiFi
   IP             192.168.1.41
   Feed           feed.ferced.com
   ─────────────────────────────────
   Encendido      2 h 14 min
   Animación      35,4 fps
   Ítems          20

   [ Actualizar feed ]      [ Cerrar ]

   [ Reaparear WiFi ]
```

Etiquetas en `FG_3`, valores en `FG`, separadores en `LINE`. `Reaparear WiFi` en
`DANGER`, separado del resto.

La fila `Feed` muestra sólo el host, no la URL completa: alcanza para distinguir
el build de LAN del de producción, y no expone la ruta ni el token.

## Entrada, salida y redibujo

- **Entra** con long press en cualquier parte. El gesto ya existe en
  `touch_manager` y está libre.
- **Sale** con `Cerrar`.
- Adentro, el tap se resuelve por coordenadas: `TouchEvent` ya las trae.

No se eligió doble tap porque el tap simple avanza ítem: dos toques rápidos para
saltear titulares entrarían a configuración sin querer. Tocar el logo tampoco,
porque está reservado para el modo Archillect.

**Los datos son una foto del momento de entrar.** No se refrescan solos. Un
repintado a pantalla completa cuesta `23,4 + 0,1575 x 480 = 99 ms` según la
ecuación medida del handoff; hacerlo cada segundo para mover el uptime sería
gastar el panel sin motivo. Sólo se repinta al entrar y al tocar un botón.

## Reaparear WiFi

Borra las credenciales de NVS y reinicia al provisioning. **Sin confirmación:**
un toque lo ejecuta.

Se propuso una confirmación de dos toques y el usuario eligió que fuera directo.
Queda anotado porque el riesgo es real: un toque accidental deja el aparato
esperando que lo apareen otra vez desde el teléfono. Se mitiga sólo con la
disposición — el botón va separado abajo y en el color de peligro.

## La versión

`tools/inject_version.py` como `extra_scripts` de PlatformIO, con el mismo
mecanismo que ya usa `inject_secrets.py`. Define tres macros:

| Macro | Origen |
|---|---|
| `FERCED_VERSION` | semántica, a mano, arranca en `1.0.0` |
| `FERCED_COMMIT` | `git rev-parse --short HEAD`, con sufijo `-dirty` si el árbol está sucio |
| `FERCED_BUILD_DATE` | fecha y hora de compilación |

El sufijo `-dirty` es el que avisa que lo flasheado no corresponde a ningún
commit, que es el caso más engañoso de todos.

No se toca `APP_VERSION` de `config.h` (`5.1.1-beta.74`, de Lemon) ni el
`v4.0.0` hardcodeado del portal. Son de otra época y unificarlos es otro
trabajo.

## Verificación

El simulador con una `ConfigInfo` falsa, agregando `ui_config.cpp` a
`sim/build.ps1`. Eso cubre la disposición, la paleta y el texto.

**Queda sin verificar hasta poder flashear:** el long press de entrada, la
resolución de los botones por coordenadas y las dos acciones. Al momento de
escribir esto el CDC del aparato está trabado y no enumera COM3.

## Lo que no hace

- No cambia ni guarda ninguna configuración.
- No refresca los datos en vivo.
- No toca las versiones heredadas de Lemon.

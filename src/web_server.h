#pragma once

#include <stdint.h>

// El servidor que hospeda el propio ESP32, en el puerto 80 de la red local:
// http://<ip-del-aparato>/ o http://ferced.local/
//
// Sirve dos cosas:
//   · el editor de la lista de tareas,
//   · la entrada de avisos, POST /api/notify, que es como los agentes de Claude
//     y Codex avisan que terminaron una tarea.
//
// Es la única parte del sistema que NO pasa por el proxy, y es a propósito: son
// datos privados, tienen que funcionar aunque el VPS esté caído, y no hay razón
// para que salgan de la red de casa.

// Levanta el servidor. Necesita WiFi ya conectado. Llamarlo dos veces no hace
// nada.
void webServerStart();

// Lo baja. Hace falta antes de volver al portal cautivo del aprovisionamiento,
// que usa el mismo puerto 80.
void webServerStop();

bool webServerRunning();

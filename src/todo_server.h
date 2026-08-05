#pragma once

#include <stdint.h>

// El editor de tareas que hospeda el propio ESP32, en el puerto 80 de la red
// local: http://<ip-del-aparato>/
//
// Es la única parte del sistema que NO pasa por el proxy, y es a propósito: la
// lista es privada, tiene que poder editarse aunque el VPS esté caído, y los
// datos nunca salen de la red de casa.

// Levanta el servidor. Necesita WiFi ya conectado. Llamarlo dos veces no hace
// nada.
void todoServerStart();

// Lo baja. Hace falta antes de volver al portal cautivo del aprovisionamiento,
// que usa el mismo puerto 80.
void todoServerStop();

bool todoServerRunning();

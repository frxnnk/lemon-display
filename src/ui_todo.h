#pragma once

#include <stdint.h>

#include "todo_store.h"
#include "ui_anim.h"

// App de tareas: una lista, no un ítem por pantalla como las otras dos.
//
// No se anima, por la misma razón que el selector de apps: las filas ocupan
// toda la pantalla y la banda sucia sería el panel entero, o sea ~99 ms por
// frame. Y una lista se lee de un vistazo, no se mira entrar.

void uiTodoSetup();

// Dibuja la lista. `direccion` es dónde editarla desde el teléfono; se muestra
// al pie y llega armada porque esta pantalla no consulta WiFi (así también
// compila en el simulador).
void uiTodoDraw(const char* direccion);

// Qué fila cae bajo un toque, o -1. Tocar una tarea la marca hecha.
int8_t uiTodoHit(int16_t x, int16_t y);

// Cuántas filas entran en pantalla.
uint8_t uiTodoVisibles();

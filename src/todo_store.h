#pragma once

#include <stdint.h>
#include <stddef.h>

// La lista de tareas y su persistencia.
//
// A diferencia de las noticias y el pádel, esto NO viene del proxy: es estado
// propio del aparato. Vive en NVS y se edita desde el servidor que el mismo
// ESP32 levanta en la red local, así que funciona aunque el VPS esté caído y
// los datos nunca salen de casa.

#define TODO_MAX_ITEMS  16
#define TODO_TEXT_LEN   64   // ~40 caracteres entran en pantalla; el resto se recorta al dibujar

struct TodoItem {
    char text[TODO_TEXT_LEN];
    bool done;
};

// Lee la lista de NVS. Llamar una vez al arrancar.
void todoLoad();

uint8_t         todoCount();
uint8_t         todoPending();
const TodoItem* todoItem(uint8_t i);

// Todas devuelven true si la lista cambió. Persisten solas.
bool todoAdd(const char* text);
bool todoToggle(uint8_t i);
bool todoRemove(uint8_t i);
bool todoClearDone();

// Sube con cada cambio. La pantalla compara contra la que dibujó para saber si
// tiene que repintar, sin que el servidor web tenga que conocer la UI ni tocar
// el sprite desde otra tarea.
uint32_t todoRevision();

#include "todo_store.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>

// Namespace propio y no el "lemon" del resto: la lista es del firmware de
// Ferced, y así un nvsFactoryReset() del código heredado no se la lleva puesta.
static const char* NS = "ferced_todo";
static const uint8_t VERSION = 1;

static TodoItem _items[TODO_MAX_ITEMS];
static uint8_t  _count = 0;
static uint32_t _rev = 0;

// El servidor web corre en la tarea de AsyncTCP y la pantalla en la del loop:
// las dos tocan la lista. Sin el mutex, agregar una tarea desde el teléfono
// justo mientras se dibuja deja la pantalla leyendo memoria a medio escribir.
static portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

uint32_t todoRevision() { return _rev; }

uint8_t todoCount() { return _count; }

uint8_t todoPending() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < _count; i++) {
        if (!_items[i].done) n++;
    }
    return n;
}

const TodoItem* todoItem(uint8_t i) { return i < _count ? &_items[i] : nullptr; }

static void guardar() {
    Preferences p;
    if (!p.begin(NS, false)) {
        Serial.println("[Todo] no pude abrir NVS para guardar");
        return;
    }
    p.putUChar("ver", VERSION);
    p.putUChar("cnt", _count);
    p.putBytes("items", _items, sizeof(TodoItem) * TODO_MAX_ITEMS);
    p.end();
}

void todoLoad() {
    Preferences p;
    if (!p.begin(NS, true)) {
        Serial.println("[Todo] sin lista guardada");
        return;
    }
    if (p.getUChar("ver", 0) == VERSION) {
        const uint8_t n = p.getUChar("cnt", 0);
        const size_t sz = sizeof(TodoItem) * TODO_MAX_ITEMS;
        if (p.getBytesLength("items") == sz) {
            p.getBytes("items", _items, sz);
            _count = n > TODO_MAX_ITEMS ? TODO_MAX_ITEMS : n;
        }
    }
    p.end();

    // Un texto sin terminador convertiría cualquier dibujado en una lectura
    // fuera de rango. Es barato asegurarlo una vez acá.
    for (uint8_t i = 0; i < _count; i++) {
        _items[i].text[TODO_TEXT_LEN - 1] = '\0';
    }
    _rev++;
    Serial.printf("[Todo] %u tareas, %u pendientes\n", _count, todoPending());
}

// Limpia el texto que llega del navegador: sin saltos de línea ni controles,
// sin espacios de más, y recortado al largo que el aparato puede guardar.
static bool limpiar(const char* in, char* out, size_t outLen) {
    if (!in) return false;
    size_t n = 0;
    bool espacioPendiente = false;
    for (const unsigned char* p = (const unsigned char*)in; *p && n < outLen - 1; p++) {
        unsigned char c = *p;
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if (c < 0x20) continue;          // controles: no se ven y ocupan
        if (c == ' ') {
            if (n > 0) espacioPendiente = true;   // se colapsan y no van al principio
            continue;
        }
        if (espacioPendiente) {
            out[n++] = ' ';
            espacioPendiente = false;
            if (n >= outLen - 1) break;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return n > 0;
}

bool todoAdd(const char* text) {
    char limpio[TODO_TEXT_LEN];
    if (!limpiar(text, limpio, sizeof(limpio))) return false;

    portENTER_CRITICAL(&_mux);
    const bool hayLugar = _count < TODO_MAX_ITEMS;
    if (hayLugar) {
        memset(&_items[_count], 0, sizeof(TodoItem));
        memcpy(_items[_count].text, limpio, sizeof(limpio));
        _items[_count].done = false;
        _count++;
        _rev++;
    }
    portEXIT_CRITICAL(&_mux);

    if (!hayLugar) return false;
    guardar();
    return true;
}

bool todoToggle(uint8_t i) {
    portENTER_CRITICAL(&_mux);
    const bool ok = i < _count;
    if (ok) {
        _items[i].done = !_items[i].done;
        _rev++;
    }
    portEXIT_CRITICAL(&_mux);

    if (!ok) return false;
    guardar();
    return true;
}

bool todoRemove(uint8_t i) {
    portENTER_CRITICAL(&_mux);
    const bool ok = i < _count;
    if (ok) {
        for (uint8_t j = i; j + 1 < _count; j++) _items[j] = _items[j + 1];
        _count--;
        memset(&_items[_count], 0, sizeof(TodoItem));
        _rev++;
    }
    portEXIT_CRITICAL(&_mux);

    if (!ok) return false;
    guardar();
    return true;
}

bool todoClearDone() {
    portENTER_CRITICAL(&_mux);
    uint8_t escritas = 0;
    for (uint8_t i = 0; i < _count; i++) {
        if (!_items[i].done) {
            if (escritas != i) _items[escritas] = _items[i];
            escritas++;
        }
    }
    const bool cambio = escritas != _count;
    if (cambio) {
        for (uint8_t i = escritas; i < _count; i++) memset(&_items[i], 0, sizeof(TodoItem));
        _count = escritas;
        _rev++;
    }
    portEXIT_CRITICAL(&_mux);

    if (!cambio) return false;
    guardar();
    return true;
}

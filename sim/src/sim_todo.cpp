// todo_store para el simulador: en vez de NVS, un archivo de texto.
//
// El servidor web no existe acá (no hay AsyncWebServer en el escritorio), pero
// no hace falta: lo que se itera en el simulador es cómo queda la lista en
// pantalla, y para eso alcanza con poder editar data/todo.txt.

#include "todo_store.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static TodoItem _items[TODO_MAX_ITEMS];
static uint8_t  _count = 0;
static uint32_t _rev = 0;

uint32_t todoRevision() { return _rev; }
uint8_t  todoCount() { return _count; }

uint8_t todoPending() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < _count; i++) {
        if (!_items[i].done) n++;
    }
    return n;
}

const TodoItem* todoItem(uint8_t i) { return i < _count ? &_items[i] : nullptr; }

// Formato: una tarea por linea. El prefijo "x " la marca como hecha.
void todoLoad() {
    const char* path = std::getenv("SIM_TODO");
    const std::string file = path ? path : "data/todo.txt";

    FILE* f = std::fopen(file.c_str(), "r");
    if (!f) {
        std::printf("[sim] sin fixture de tareas (%s): lista vacia\n", file.c_str());
        _count = 0;
        _rev++;
        return;
    }

    _count = 0;
    char line[256];
    while (std::fgets(line, sizeof(line), f) && _count < TODO_MAX_ITEMS) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;

        TodoItem& it = _items[_count];
        std::memset(&it, 0, sizeof(it));
        it.done = s.size() > 1 && (s[0] == 'x' || s[0] == 'X') && s[1] == ' ';
        std::snprintf(it.text, TODO_TEXT_LEN, "%s", it.done ? s.c_str() + 2 : s.c_str());
        if (it.text[0]) _count++;
    }
    std::fclose(f);
    _rev++;
    std::printf("[sim] tareas: %u, %u pendientes\n", _count, todoPending());
}

bool todoAdd(const char* text) {
    if (!text || !text[0] || _count >= TODO_MAX_ITEMS) return false;
    std::memset(&_items[_count], 0, sizeof(TodoItem));
    std::snprintf(_items[_count].text, TODO_TEXT_LEN, "%s", text);
    _count++;
    _rev++;
    return true;
}

bool todoToggle(uint8_t i) {
    if (i >= _count) return false;
    _items[i].done = !_items[i].done;
    _rev++;
    return true;
}

bool todoRemove(uint8_t i) {
    if (i >= _count) return false;
    for (uint8_t j = i; j + 1 < _count; j++) _items[j] = _items[j + 1];
    _count--;
    _rev++;
    return true;
}

bool todoClearDone() {
    uint8_t escritas = 0;
    for (uint8_t i = 0; i < _count; i++) {
        if (!_items[i].done) _items[escritas++] = _items[i];
    }
    if (escritas == _count) return false;
    _count = escritas;
    _rev++;
    return true;
}

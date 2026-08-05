#include "notif_store.h"

#include <Arduino.h>
#include <cstring>

// Anillo con el más nuevo primero. Ocho alcanza: esto es una pantalla de
// escritorio, no una bandeja de entrada.
static Notif   _items[NOTIF_MAX];
static uint8_t _count = 0;
static uint32_t _rev = 0;

// El servidor corre en la tarea de AsyncTCP y la pantalla en la del loop, igual
// que con las tareas. Sin el mutex, un aviso que llega justo mientras se dibuja
// deja la pantalla leyendo memoria a medio escribir.
static portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

uint32_t notifRevision() { return _rev; }
uint8_t  notifCount() { return _count; }

const Notif* notifAt(uint8_t i) { return i < _count ? &_items[i] : nullptr; }

uint8_t notifSinLeer() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < _count; i++) {
        if (!_items[i].leido) n++;
    }
    return n;
}

const Notif* notifPendiente() {
    // El más nuevo sin leer. Si llegaron tres de golpe se muestra el último y
    // los demás quedan en la lista: interrumpir tres veces seguidas sería peor
    // que no interrumpir.
    for (uint8_t i = 0; i < _count; i++) {
        if (!_items[i].leido) return &_items[i];
    }
    return nullptr;
}

void notifMarcarTodosLeidos() {
    portENTER_CRITICAL(&_mux);
    for (uint8_t i = 0; i < _count; i++) _items[i].leido = true;
    _rev++;
    portEXIT_CRITICAL(&_mux);
}

void notifBorrarTodos() {
    portENTER_CRITICAL(&_mux);
    _count = 0;
    memset(_items, 0, sizeof(_items));
    _rev++;
    portEXIT_CRITICAL(&_mux);
}

// Deja el texto en una línea, sin controles ni espacios de más. Lo que llega es
// la salida de un agente: puede traer saltos, tabulaciones y colas larguísimas.
static bool limpiar(const char* in, char* out, size_t outLen) {
    if (!in) { out[0] = '\0'; return false; }
    size_t n = 0;
    bool espacio = false;
    for (const unsigned char* p = (const unsigned char*)in; *p && n < outLen - 1; p++) {
        unsigned char c = *p;
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if (c < 0x20) continue;
        if (c == ' ') {
            if (n > 0) espacio = true;
            continue;
        }
        if (espacio) {
            out[n++] = ' ';
            espacio = false;
            if (n >= outLen - 1) break;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return n > 0;
}

bool notifPush(const char* src, const char* title, const char* body, uint32_t epoch) {
    Notif nuevo;
    memset(&nuevo, 0, sizeof(nuevo));

    if (!limpiar(title, nuevo.title, sizeof(nuevo.title))) return false;
    limpiar(body, nuevo.body, sizeof(nuevo.body));

    if (!limpiar(src, nuevo.src, sizeof(nuevo.src))) {
        snprintf(nuevo.src, sizeof(nuevo.src), "AVISO");
    }
    for (char* c = nuevo.src; *c; c++) *c = toupper((unsigned char)*c);

    nuevo.epoch = epoch;
    nuevo.ms = millis();
    nuevo.leido = false;

    portENTER_CRITICAL(&_mux);
    // Corrimiento hacia abajo: el más nuevo va primero y el más viejo se cae.
    for (uint8_t i = (_count < NOTIF_MAX ? _count : NOTIF_MAX - 1); i > 0; i--) {
        _items[i] = _items[i - 1];
    }
    _items[0] = nuevo;
    if (_count < NOTIF_MAX) _count++;
    _rev++;
    portEXIT_CRITICAL(&_mux);

    Serial.printf("[Notif] %s: %s\n", nuevo.src, nuevo.title);
    return true;
}

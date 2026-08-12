// notif_store para el simulador: en vez de llegar por HTTP, los avisos salen de
// un archivo. Lo que se itera acá es cómo quedan en pantalla.

#include "notif_store.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static Notif    _items[NOTIF_MAX];
static uint8_t  _count = 0;
static uint32_t _rev = 0;

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
    for (uint8_t i = 0; i < _count; i++) {
        if (!_items[i].leido) return &_items[i];
    }
    return nullptr;
}

void notifMarcarTodosLeidos() {
    for (uint8_t i = 0; i < _count; i++) _items[i].leido = true;
    _rev++;
}

void notifBorrarTodos() {
    _count = 0;
    _rev++;
}

bool notifPush(const char* src, const char* title, const char* body, uint32_t epoch) {
    if (!title || !title[0] || _count >= NOTIF_MAX) return false;
    for (uint8_t i = (_count < NOTIF_MAX ? _count : NOTIF_MAX - 1); i > 0; i--) {
        _items[i] = _items[i - 1];
    }
    Notif& n = _items[0];
    std::memset(&n, 0, sizeof(n));
    std::snprintf(n.src, NOTIF_SRC_LEN, "%s", src && src[0] ? src : "AVISO");
    for (char* c = n.src; *c; c++) *c = (char)toupper((unsigned char)*c);
    std::snprintf(n.title, NOTIF_TITLE_LEN, "%s", title);
    std::snprintf(n.body, NOTIF_BODY_LEN, "%s", body ? body : "");
    n.epoch = epoch;
    n.ms = 0;
    n.leido = false;
    if (_count < NOTIF_MAX) _count++;
    _rev++;
    return true;
}

// Formato: fuente|titulo|cuerpo. El prefijo "x " en la fuente lo marca leído.
void simNotifLoad() {
    const char* path = std::getenv("SIM_NOTIF");
    const std::string file = path ? path : "data/avisos.txt";

    FILE* f = std::fopen(file.c_str(), "r");
    if (!f) {
        std::printf("[sim] sin fixture de avisos (%s)\n", file.c_str());
        return;
    }

    std::vector<std::string> lineas;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty() && s[0] != '#') lineas.push_back(s);
    }
    std::fclose(f);

    // Al revés, para que el primero del archivo termine siendo el más nuevo.
    for (auto it = lineas.rbegin(); it != lineas.rend(); ++it) {
        std::vector<std::string> p;
        size_t start = 0;
        for (size_t i = 0; i <= it->size(); i++) {
            if (i == it->size() || (*it)[i] == '|') {
                p.push_back(it->substr(start, i - start));
                start = i + 1;
            }
        }
        if (p.size() < 2) continue;
        bool leido = p[0].size() > 1 && (p[0][0] == 'x' || p[0][0] == 'X') && p[0][1] == ' ';
        notifPush(leido ? p[0].c_str() + 2 : p[0].c_str(), p[1].c_str(),
                  p.size() > 2 ? p[2].c_str() : "", 0);
        if (leido) _items[0].leido = true;
    }
    std::printf("[sim] avisos: %u, %u sin leer\n", _count, notifSinLeer());
}

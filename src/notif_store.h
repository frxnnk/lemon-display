#pragma once

#include <stdint.h>

// Avisos que mandan los agentes cuando terminan una tarea.
//
// Deliberadamente NO se persisten. Un aviso de "terminó la compilación" de antes
// de un reinicio no le sirve a nadie: lo que importa de una notificación es que
// llegue ahora. Guardarla en flash sería gastar escrituras para mostrar ruido.

#define NOTIF_MAX        8
#define NOTIF_SRC_LEN   14   // "CLAUDE", "CODEX"
#define NOTIF_TITLE_LEN 72
#define NOTIF_BODY_LEN 120

struct Notif {
    char     src[NOTIF_SRC_LEN];
    char     title[NOTIF_TITLE_LEN];
    char     body[NOTIF_BODY_LEN];
    uint32_t epoch;    // cuándo llegó, en epoch UTC; 0 si el reloj no estaba listo
    uint32_t ms;       // millis() de llegada, para el "hace tanto" sin NTP
    bool     leido;
};

// Guarda un aviso. Devuelve false si el título quedó vacío después de limpiarlo.
bool notifPush(const char* src, const char* title, const char* body, uint32_t epoch);

uint8_t      notifCount();
uint8_t      notifSinLeer();
const Notif* notifAt(uint8_t i);      // 0 es el más nuevo

// El más nuevo sin leer, o nullptr. Es lo que la pantalla interrumpe a mostrar.
const Notif* notifPendiente();

void notifMarcarTodosLeidos();
void notifBorrarTodos();

// Sube con cada cambio, para que la pantalla sepa que tiene que repintar sin
// que la tarea del servidor toque el panel.
uint32_t notifRevision();

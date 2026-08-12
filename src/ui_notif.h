#pragma once

#include <stdint.h>

#include "notif_store.h"
#include "ui_anim.h"

// Los avisos tienen dos caras:
//
//   · la tarjeta que INTERRUMPE cuando llega uno, que es el punto de todo esto;
//   · la lista de los últimos, para cuando no estabas mirando.
//
// Ninguna se anima. Un aviso que entra escalonado durante un segundo es un
// aviso que llega tarde: tiene que estar cuando levantás la vista.

void uiNotifSetup();

// La tarjeta a pantalla completa. `restante01` (1..0) dibuja la barra que se
// agota hasta el cierre automático.
void uiNotifDrawCard(const Notif* n, float restante01);

// Repinta SÓLO la barra. Se llama cuatro veces por segundo, y repintar la
// pantalla entera para eso costaría 99 ms cada vez.
void uiNotifDrawBarra(float restante01);

// La lista de los últimos avisos.
void uiNotifDrawLista();

// Qué fila cae bajo un toque en la lista, o -1.
int8_t uiNotifHit(int16_t x, int16_t y);

// true si el toque cayo en el boton de cerrar de la tarjeta que interrumpe.
// Antes cualquier toque la cerraba, asi que un roce se llevaba el aviso puesto
// antes de que llegaras a leerlo.
bool uiNotifHitCerrar(int16_t x, int16_t y);

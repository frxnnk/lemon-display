#pragma once

#include <stdint.h>

#include "feed_client.h"
#include "ui_anim.h"   // FercedColors, UiFrameStats y el motor de animación

// App de noticias: un titular por pantalla, con miniatura y entrada escalonada.

void uiFercedSetup();

// Arranca la transición hacia un ítem. No dibuja: la animación la avanza
// uiFercedTick(), así el bucle principal nunca se bloquea.
void uiFercedShowItem(const FeedItem* item, uint8_t index, uint8_t total,
                      bool offline);

void uiFercedShowStatus(const char* eyebrow, const char* message);

// Avanza la animación y repinta lo que haga falta. Llamar en cada vuelta del
// loop. `progress01` (0..1) alimenta la línea de progreso hacia el próximo
// ítem. Devuelve true mientras haya animación en curso.
bool uiFercedTick(uint32_t nowEpoch, float progress01);

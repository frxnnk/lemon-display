#pragma once

#include <stdint.h>

#include "padel_client.h"
#include "ui_anim.h"

// App de pádel: una pantalla por partido del orden de juego del día, más una
// tarjeta por torneo (el que se está jugando y los que vienen).

void uiPadelSetup();

// Prepara la pantalla número `screen` (0..padelScreenCount()-1) y arranca su
// entrada. El orden es: torneo en juego, partidos del día, próximos torneos.
void uiPadelShowScreen(uint8_t screen);

void uiPadelShowStatus(const char* eyebrow, const char* message);

// Avanza la animación. `progress01` (0..1) alimenta la línea de progreso hacia
// la próxima pantalla. Devuelve true mientras haya animación en curso.
bool uiPadelTick(float progress01);

#pragma once

#include <stdint.h>

#include "apps.h"
#include "ui_anim.h"

// El selector de apps. Se abre con un deslizamiento hacia arriba y se cierra
// tocando la app que se quiere, o deslizando hacia abajo.
//
// No se anima, a propósito. Una entrada escalonada costaría un repintado casi
// completo por frame (~99 ms, o sea 10 fps) porque las tarjetas son altas y la
// banda sucia termina siendo la pantalla entera. Y además un menú no se mira,
// se usa: aparece entero de una y se elige. Es la misma decisión que en la
// pantalla de configuración.

void uiLauncherSetup();

// Dibuja el selector. `apps` tiene `n` entradas y `actual` es la que está
// corriendo, que se dibuja destacada.
void uiLauncherDraw(const AppInfo* apps, uint8_t n, uint8_t actual);

// Qué tarjeta cae bajo un toque, o -1 si ninguna. La geometría vive en el .cpp
// y la comparten el dibujo y esto, para que no puedan desincronizarse.
int8_t uiLauncherHit(int16_t x, int16_t y);

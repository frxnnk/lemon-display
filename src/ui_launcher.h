#pragma once

#include <stdint.h>

#include "apps.h"
#include "ui_anim.h"

// El selector de apps. Se abre con un deslizamiento hacia arriba y se cierra
// tocando la app que se quiere, o deslizando hacia abajo.
//
// No entra escalonado como noticias o pádel: sus baldosas son altas y la banda
// sucia terminaría siendo la pantalla entera, o sea 10 fps. Se compone entero
// sobre el sprite y se revela con la cortina de uiAnimReveal(), que baja de
// arriba hacia abajo en bandas que sí entran en el presupuesto. Es la misma
// decisión que en la pantalla de configuración.

// Seis, no cuatro: la grilla es de dos columnas. Con una sola columna la
// cuarta app ya bajaba las tarjetas a 60 px y el texto se cruzaba con el borde.
constexpr uint8_t UI_LAUNCHER_MAX_APPS = 6;

void uiLauncherSetup();

// Dibuja el selector. `apps` tiene `n` entradas y `actual` es la que está
// corriendo, que se dibuja destacada.
void uiLauncherDraw(const AppInfo* apps, uint8_t n, uint8_t actual);

// Qué tarjeta cae bajo un toque, o -1 si ninguna. La geometría vive en el .cpp
// y la comparten el dibujo y esto, para que no puedan desincronizarse.
int8_t uiLauncherHit(int16_t x, int16_t y);

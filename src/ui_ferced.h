#pragma once

#include <stdint.h>
#include "feed_client.h"

// Paleta Ferced en RGB565. Los grises no son grises inventados: son blanco
// con alpha sobre el canvas, como manda la identidad.
namespace FercedColors {
    constexpr uint16_t CANVAS  = 0x0842;  // #0e1011
    constexpr uint16_t CARD    = 0x0841;  // #0a0a0a
    constexpr uint16_t SURFACE = 0x18E3;  // blanco 4% sobre canvas
    constexpr uint16_t FG      = 0xFFFF;  // #ffffff
    constexpr uint16_t FG_2    = 0xBDF7;  // blanco 75%
    constexpr uint16_t FG_3    = 0x8C71;  // blanco 55%
    constexpr uint16_t FG_4    = 0x632C;  // blanco 40%
    constexpr uint16_t LINE    = 0x18E3;  // blanco 10%
    constexpr uint16_t SUCCESS = 0x36F3;  // #34d399 — sólo estado
    constexpr uint16_t DANGER  = 0xFB8E;  // #f87171 — sólo estado
}

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

// Medición real de la última animación, para no discutir el framerate a ojo.
struct UiFrameStats {
    uint16_t frames;    // frames dibujados en la última transición
    uint16_t avgUs100;  // duración media de frame, en centenas de microsegundo
    uint16_t worstUs100;// peor frame
};
UiFrameStats uiFercedStats();

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

// Dibuja un ítem del feed. `index`/`total` alimentan los puntos de posición.
// `offline` enciende la marca sutil de desconexión.
void uiFercedDrawItem(const FeedItem* item, uint8_t index, uint8_t total,
                      uint32_t nowEpoch, bool offline);

// Pantalla de estado (arranque, sin red, feed vacío).
void uiFercedDrawStatus(const char* eyebrow, const char* message);

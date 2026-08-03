#pragma once

// Los headers de bitmaps generados por tools/png_to_rgb565.py incluyen
// <pgmspace.h>, que solo existe en el ESP32. En el host la memoria de programa
// y la de datos son la misma, asi que PROGMEM no hace nada y pgm_read_* es una
// lectura directa.

#include <cstdint>

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#endif

#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#endif

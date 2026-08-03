#pragma once

// Lo minimo de Arduino que usa ui_ferced.cpp, para compilarlo en el host sin
// modificar una linea del archivo original.

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define PROGMEM
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))

inline uint32_t micros() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<microseconds>(steady_clock::now() - t0).count();
}

inline uint32_t millis() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

// El firmware imprime diagnosticos por Serial; aca van a la consola.
struct SerialShim {
    void begin(unsigned long) {}
    void println(const char* s) { std::printf("%s\n", s); }
    void print(const char* s) { std::printf("%s", s); }
    template <typename... A>
    void printf(const char* fmt, A... a) { std::printf(fmt, a...); }
};
static SerialShim Serial;

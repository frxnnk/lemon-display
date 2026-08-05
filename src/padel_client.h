#pragma once

#include <stdint.h>

// Cliente de /v1/padel. Igual que con el feed, el aparato no sabe de donde
// salen los datos: el proxy raspa padelfip.com y el widget de orden de juego, y
// entrega texto ya limpio, en castellano y del largo que entra en pantalla.

#define PADEL_MAX_MATCHES  12
#define PADEL_MAX_TOURS     6

#define PADEL_NAME_LEN     34   // "S. Pineda Cabello" entra holgado
#define PADEL_COURT_LEN    24   // "CENTER COURT"
#define PADEL_TIME_LEN     22   // "no antes de 15:30"

struct PadelMatch {
    char time[PADEL_TIME_LEN];
    char court[PADEL_COURT_LEN];
    char round[14];              // "R32", "Octavos", "FINAL"
    char gender;                 // 'M', 'F' o 0
    char a1[PADEL_NAME_LEN];
    char a2[PADEL_NAME_LEN];
    char b1[PADEL_NAME_LEN];
    char b2[PADEL_NAME_LEN];
    char seedA[5];
    char seedB[5];
    char scoreA[14];             // "6 4 6"
    char scoreB[14];
    uint8_t state;               // 0 por jugar, 1 jugando, 2 terminado
};

struct PadelTour {
    char name[42];
    char cat[12];                // "MAJOR", "P1", "GOLD"
    char city[28];
    char country[28];
    char rango[20];              // "2-9 ago"
    uint16_t faltan;             // dias hasta el inicio
    bool live;
    uint8_t day, days;           // solo si live
};

enum PadelResult : uint8_t {
    PADEL_UPDATED,
    PADEL_STALE_CACHE,   // fallo la red, se conserva lo anterior
    PADEL_FAILED,
};

PadelResult padelFetch();

uint8_t           padelMatchCount();
const PadelMatch* padelMatch(uint8_t i);
uint8_t           padelTourCount();
const PadelTour*  padelTour(uint8_t i);

// El torneo en juego, o nullptr si no hay ninguno de las categorias elegidas.
const PadelTour*  padelLive();

// La fecha del orden de juego, ya en castellano ("mié 5 ago"), o "" si no se
// pudo determinar. La hora sola no dice cuándo se juega.
const char*       padelFecha();

// Cuantas pantallas tiene la app: el torneo en juego (si hay), mas un partido
// por pantalla, mas los proximos torneos.
uint8_t padelScreenCount();

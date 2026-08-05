#pragma once

#include <stdint.h>

// Las apps del aparato. Agregar una es sumar un valor acá, una entrada en la
// tabla de ferced_main.cpp y su par ui_*/cliente: el launcher y los gestos no
// se tocan.
enum AppId : uint8_t {
    APP_NOTICIAS,
    APP_PADEL,
    APP_COUNT,
};

// Lo que el launcher necesita saber de una app. El estado lo arma quien la
// conoce —cuántos titulares hay, qué torneo se juega— y llega ya formateado:
// la pantalla no consulta clientes.
struct AppInfo {
    const char* nombre;    // "NOTICIAS"
    char        inicial;   // el monograma de la tarjeta
    char        estado[52];
};

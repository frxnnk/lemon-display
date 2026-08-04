#pragma once

#include <cstdint>

// Los datos llegan armados a proposito: si esta pantalla consultara WiFi o NVS
// adentro no compilaria en el simulador, que solo tiene shims de graficos.
struct ConfigInfo {
    const char* version;
    const char* commit;
    const char* built;
    const char* ssid;
    const char* ip;
    const char* endpoint;   // solo el host
    uint32_t    uptimeS;
    float       fps;
    uint8_t     items;
    bool        online;
};

enum ConfigAction : uint8_t {
    CFG_NONE,
    CFG_REFRESH,
    CFG_UPDATE,
    CFG_CLOSE,
    CFG_FORGET,
};

void uiConfigDraw(const ConfigInfo& info);

// Escribe la franja de estado que queda entre los datos y los botones, y NADA
// mas: repinta solo su rectangulo, sin fillScreen. Es lo que permite mostrar
// el avance de una descarga, que llega hasta 101 veces mientras la pantalla
// entera cuesta 99 ms por repintado.
//
//   pct  < 0  solo el texto (un aviso, un error)
//   pct >= 0  ademas la barra y el porcentaje
//   error     pinta el texto en rojo
void uiConfigEstado(const char* texto, int pct = -1, bool error = false);

// Resuelve que boton cae bajo un toque. La geometria vive en el .cpp y la usan
// tanto el dibujo como esto, para que no puedan desincronizarse.
ConfigAction uiConfigHit(int16_t x, int16_t y);

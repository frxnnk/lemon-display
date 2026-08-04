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
    CFG_CLOSE,
    CFG_FORGET,
};

void uiConfigDraw(const ConfigInfo& info);

// Resuelve que boton cae bajo un toque. La geometria vive en el .cpp y la usan
// tanto el dibujo como esto, para que no puedan desincronizarse.
ConfigAction uiConfigHit(int16_t x, int16_t y);

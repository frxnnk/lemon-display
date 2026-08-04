#pragma once

// Actualizacion por WiFi desde el proxy.
//
// El binario baja de /v1/firmware/bin, el mismo servidor que sirve el feed y
// con el mismo Bearer token. La diferencia con feed_client.cpp es que aca el
// certificado del servidor SE VALIDA contra las raices de Let's Encrypt
// embebidas en src/data/le_roots.h: un feed de noticias es texto, un firmware
// es codigo que el aparato va a ejecutar.
//
// Ademas se verifica el sha256 que declara el proxy mientras el binario baja,
// y se aborta antes de confirmar la particion si no coincide.

#include <cstddef>
#include <cstdint>

struct OtaCheck {
    bool  hayNueva;
    bool  error;
    char  version[16];
    char  detalle[48];   // para mostrar en pantalla si algo falla
    uint32_t size;
};

// Consulta /v1/firmware y compara con FERCED_VERSION.
OtaCheck otaBuscar();

// Descarga y escribe en la particion inactiva. progreso recibe 0..100.
// Devuelve false y deja el motivo en detalle si falla. Si tiene exito reinicia.
bool otaAplicar(void (*progreso)(int pct), char* detalle, size_t detalleLen);

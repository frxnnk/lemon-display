#pragma once

#include <stdint.h>

// La pantalla de aprovisionamiento: el QR para aparear el WiFi.
//
// Es lo PRIMERO que ve alguien que enchufa el aparato por primera vez, y hasta
// la 1.4.0 era la unica que no se podia mirar sin flashear —el simulador
// pediria stubear WiFi, AsyncWebServer y QRCode— asi que era tambien la unica
// que nunca se habia verificado en el panel.
//
// Se arregla partiendola en dos: wifi_provision.cpp sigue levantando el AP y
// calculando el QR, y el dibujo vive aca, sobre el sprite, recibiendo los
// modulos ya resueltos. Asi el simulador puede componerla sin saber nada de
// redes, y el firmware le pasa el QR de verdad.

// `modulos` es el QR fila por fila, un byte por modulo: 0 apagado, 1 encendido.
// `lado` es cuantos modulos tiene de lado (41 para la version 6).
// `nombre` es como se llama el aparato, o nullptr si todavia no tiene.
void uiProvisionDraw(const uint8_t* modulos, uint8_t lado, const char* nombre,
                     const char* ssid, const char* pass, const char* url);

#include "ui_provision.h"

#include "config.h"
#include "design_system.h"
#include "ui_chrome.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

using namespace FercedColors;
using namespace FercedChrome;

namespace {

LGFX_Sprite& g = uiSprite;

// El QR va sobre papel blanco porque es lo que los lectores esperan: invertido
// muchos telefonos no lo enganchan. Es la misma decision que ya habia, y es de
// las pocas veces que el aparato dibuja algo claro.
constexpr uint16_t PAPEL = 0xFFFF;

constexpr int MARK_Y   = 22;
constexpr int RULE_Y_  = 98;

constexpr int MODULO   = 5;     // pixeles por modulo del QR
constexpr int QR_PAD   = 12;    // zona tranquila alrededor, la pide el estandar

constexpr int TXT1_Y   = 356;   // que hacer
constexpr int TXT2_Y   = 390;   // la red y la clave
constexpr int TXT3_Y   = 412;   // la alternativa por navegador

}  // namespace

void uiProvisionDraw(const uint8_t* modulos, uint8_t lado, const char* nombre,
                     const char* ssid, const char* pass, const char* url) {
    g.fillScreen(CANVAS);

    // Encabezado: el isotipo a color y el wordmark, igual que el selector y
    // configuracion. Es donde el aparato dice quien es, y esta es la primera vez
    // que alguien lo mira.
    uiMarkColor(g, MARGIN, MARK_Y);
    g.setFont(DS::fontDataLg());
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(FG, CANVAS);
    g.drawString("FERCED", MARGIN + UI_MARK_C_W + 18, MARK_Y + UI_MARK_C_H / 2);

    if (nombre && nombre[0]) {
        // Buffer propio y no NVS_NOMBRE_LEN: esta pantalla recibe el nombre ya
        // resuelto y no tiene por que saber de donde sale. Asi tampoco arrastra
        // nvs_storage.h al simulador, que no tiene Preferences.
        char n[24];
        snprintf(n, sizeof(n), "%s", nombre);
        for (char* c = n; *c; c++) *c = toupper((unsigned char)*c);
        g.setFont(DS::fontCaption());
        g.setTextDatum(lgfx::middle_right);
        g.setTextColor(FG_4, CANVAS);
        g.drawString(n, SCREEN_W - MARGIN, MARK_Y + UI_MARK_C_H / 2);
    }

    g.drawFastHLine(MARGIN, RULE_Y_, SCREEN_W - 2 * MARGIN, LINE);

    // El QR, centrado, sobre su tarjeta blanca.
    const int qrLado = lado * MODULO;
    const int tarjeta = qrLado + 2 * QR_PAD;
    const int qx = (SCREEN_W - tarjeta) / 2;
    const int qy = RULE_Y_ + 18;

    g.fillSmoothRoundRect(qx, qy, tarjeta, tarjeta, 14, PAPEL);
    if (modulos) {
        for (int y = 0; y < lado; y++) {
            for (int x = 0; x < lado; x++) {
                if (!modulos[y * lado + x]) continue;
                g.fillRect(qx + QR_PAD + x * MODULO, qy + QR_PAD + y * MODULO,
                           MODULO, MODULO, 0x0000);
            }
        }
    }

    // Que hacer. Una sola frase, centrada como el QR: esta pantalla es un
    // cartel, no una pantalla de contenido, y por eso es la unica del aparato
    // que centra el texto.
    g.setTextDatum(lgfx::top_center);
    g.setFont(DS::fontHeading());
    g.setTextColor(FG, CANVAS);
    g.drawString("Escaneá para conectarlo a tu WiFi", SCREEN_W / 2, TXT1_Y);

    char cred[80];
    snprintf(cred, sizeof(cred), "%s  ·  %s", ssid ? ssid : "", pass ? pass : "");
    g.setFont(DS::fontCaption());
    g.setTextColor(FG_3, CANVAS);
    g.drawString(cred, SCREEN_W / 2, TXT2_Y);

    snprintf(cred, sizeof(cred), "o entrá a %s", url ? url : "");
    g.setTextColor(FG_4, CANVAS);
    g.drawString(cred, SCREEN_W / 2, TXT3_Y);

    // El riel entero: es una pantalla donde el aparato habla de si mismo, como
    // el selector y configuracion, asi que le corresponde el espectro completo.
    uiRail(g, RAIL_Y, 1.0f);

    uiAnimReveal();
}

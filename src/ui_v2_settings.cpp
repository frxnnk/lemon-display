#include "ui_v2_settings.h"

#include "config.h"
#include "data/satoshi_fonts.h"
#include "ui_v2_runtime.h"
#include <Arduino.h>

static void drawBackChevron(LGFX_Sprite& sprite, int x, int y) {
    uint16_t accent = v2UiColorAccent();
    sprite.drawWideLine(x + 5, y - 7, x - 2, y, 2.5f, accent);
    sprite.drawWideLine(x - 2, y, x + 5, y + 7, 2.5f, accent);
}

static void drawSettingRow(LGFX_Sprite& sprite, int y,
                           const char* label, const char* value,
                           uint16_t color = 0) {
    uint16_t bg = v2UiColorBg();
    uint16_t muted = v2UiColorMuted();
    uint16_t valColor = color ? color : v2UiColorAccent();
    sprite.drawFastHLine(32, y + 51, 416, muted);
    sprite.setTextDatum(lgfx::middle_left);
    sprite.setTextColor(muted, bg);
    sprite.drawString(label, 40, y + 25, &Satoshi9);
    sprite.setTextDatum(lgfx::middle_right);
    sprite.setTextColor(valColor, bg);
    sprite.drawString(value, 440, y + 25, &Satoshi12);
}

static void drawDisplayPage(LGFX_Sprite& sprite, const V2RuntimeSnapshot& snapshot) {
    char brightness[20];
    snprintf(brightness, sizeof(brightness), "%u / 255", snapshot.brightness);
    drawSettingRow(sprite, 126, "BRILLO", brightness);
    drawSettingRow(sprite, 184, "FORMATO DE HORA", snapshot.use24h ? "24H" : "12H");
    drawSettingRow(sprite, 242, "SONIDO", snapshot.soundEnabled ? "ACTIVO" : "APAGADO");
    char rotation[24];
    if (snapshot.rotationSeconds) snprintf(rotation, sizeof(rotation), "%u SEG", snapshot.rotationSeconds);
    else snprintf(rotation, sizeof(rotation), "FIJO");
    drawSettingRow(sprite, 300, "ROTACION", rotation);
    uint8_t pair = snapshot.selectedPair < BTC_PAIR_COUNT ? snapshot.selectedPair : 0;
    drawSettingRow(sprite, 358, "ACTIVO PRINCIPAL", BTC_PAIRS[pair].pairLabel, v2UiColorHighlight());
}

static void drawDataPage(LGFX_Sprite& sprite, const V2RuntimeSnapshot& snapshot) {
    char watchlist[32];
    snprintf(watchlist, sizeof(watchlist), "%u ACTIVOS / TOCA", snapshot.stockCount);
    drawSettingRow(sprite, 126, "WATCHLIST", watchlist);
    drawSettingRow(sprite, 242, "NOTICIAS", "PROVIDER PENDIENTE", v2UiColorMuted());
    drawSettingRow(sprite, 300, "FRECUENCIA DE DATOS", "BTC LIVE / META 5M");
    drawSettingRow(sprite, 358, "STUDIO / WATCHLIST", snapshot.ip, v2UiColorHighlight());
    uint16_t wifiColor = snapshot.online ? v2UiColorAccent() : v2UiColorHighlight();
    drawSettingRow(sprite, 184, "WI-FI",
                   snapshot.wifiResetArmed ? "MANTENE PARA CONFIRMAR" : snapshot.ssid,
                   wifiColor);
}

static void drawDevicePage(LGFX_Sprite& sprite, const V2RuntimeSnapshot& snapshot) {
    char version[32];
    snprintf(version, sizeof(version), "V%s V2 CANARY", APP_VERSION);
    drawSettingRow(sprite, 126, "FIRMWARE", version);
    char diagnostic[44];
    snprintf(diagnostic, sizeof(diagnostic), "RSSI %ld / HEAP %luK",
             static_cast<long>(snapshot.rssi),
             static_cast<unsigned long>(snapshot.freeHeap / 1024));
    drawSettingRow(sprite, 184, "DIAGNOSTICO", diagnostic);
    char update[40];
    uint16_t updateColor = v2UiColorHighlight();
    if (snapshot.otaChecking) snprintf(update, sizeof(update), "BUSCANDO...");
    else if (snapshot.otaAvailable && snapshot.otaArmed) snprintf(update, sizeof(update), "CONFIRMAR UPDATE / TOCA");
    else if (snapshot.otaAvailable) snprintf(update, sizeof(update), "V%s / TOCA", snapshot.otaVersion);
    else if (snapshot.otaChecked) snprintf(update, sizeof(update), "AL DIA / TOCA BUSCAR");
    else {
        snprintf(update, sizeof(update), "BUSQUEDA AUTOMATICA");
        updateColor = v2UiColorMuted();
    }
    drawSettingRow(sprite, 242, "ACTUALIZACION", update, updateColor);
    char uptime[28];
    snprintf(uptime, sizeof(uptime), "%luh %lum",
             static_cast<unsigned long>(snapshot.uptimeSeconds / 3600),
             static_cast<unsigned long>((snapshot.uptimeSeconds / 60) % 60));
    drawSettingRow(sprite, 300, "UPTIME", uptime);
    drawSettingRow(sprite, 358, "ROLLBACK", "BINARIO LOCAL PRE-FLASH", v2UiColorHighlight());
}

void v2DrawSettings(LGFX_Sprite& sprite,
                    const V2RuntimeSnapshot& snapshot,
                    const V2RuntimeModel& model) {
    uint16_t bg = v2UiColorBg();
    uint16_t accent = v2UiColorAccent();
    uint16_t highlight = v2UiColorHighlight();
    uint16_t muted = v2UiColorMuted();
    sprite.setTextColor(accent, bg);
    sprite.setTextDatum(lgfx::top_left);
    drawBackChevron(sprite, 42, 45);
    sprite.drawString("AJUSTES", 62, 36, &SatoshiMedium18);
    const char* tabs[] = {"PANTALLA", "DATOS", "DISPOSITIVO"};
    for (uint8_t i = 0; i < 3; i++) {
        int x = 32 + i * 139;
        sprite.setTextColor(i == model.settingsPage ? highlight : muted, bg);
        sprite.drawString(tabs[i], x, 88, &Satoshi9);
        if (i == model.settingsPage) sprite.drawFastHLine(x, 108, 112, highlight);
    }
    if (model.settingsPage == 0) drawDisplayPage(sprite, snapshot);
    else if (model.settingsPage == 1) drawDataPage(sprite, snapshot);
    else drawDevicePage(sprite, snapshot);
    sprite.setTextColor(muted, bg);
    sprite.setTextDatum(lgfx::bottom_center);
    sprite.drawString("TOCA UNA SECCION O DESLIZA", 240, 466, &Satoshi9);
}

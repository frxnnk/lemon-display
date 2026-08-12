#include "ui_anim.h"

#include "config.h"
#include "ui_chrome.h"

#include <Arduino.h>

using namespace FercedColors;

LGFX_Sprite uiSprite(&tft);
static bool s_ready = false;

static uint32_t s_animStart = 0;

// Medicion del frame: sin esto el framerate es una suposicion.
static uint32_t s_frameCount = 0;
static uint32_t s_frameSumUs = 0;
static uint32_t s_frameWorstUs = 0;
// El costo dentro del tick no es el framerate: afuera quedan el delay() del
// loop, el sondeo del tactil y wifiLoop(). Para no inflar el numero, se mide
// tambien el periodo real entre arranques de frame.
static uint32_t s_lastFrameUs = 0;
static uint32_t s_framePeriodSumUs = 0;
static uint32_t s_framePeriodCount = 0;
// 250 ms son diez veces el periodo del panel: por encima de eso no hubo un
// frame lento, hubo otra cosa ocupando el loop.
static constexpr uint32_t MAX_PERIOD_US = 250000;
static UiFrameStats s_stats = {0, 0, 0, 0};

UiFrameStats uiAnimStats() { return s_stats; }

bool uiAnimReady() { return s_ready; }

void uiAnimSetup() {
    if (s_ready) return;
    uiSprite.setPsram(true);
    uiSprite.setColorDepth(16);
    uiSprite.createSprite(SCREEN_W, SCREEN_H);
    s_ready = true;
}

float uiAnimEase(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv * inv * inv;
}

uint16_t uiAnimLerp(uint16_t fg, uint16_t bg, float t) {
    if (t >= 1.0f) return fg;
    if (t <= 0.0f) return bg;
    const int fr = (fg >> 11) & 0x1F, fgr = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    const int br = (bg >> 11) & 0x1F, bgr = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    const int r = br + (int)((fr - br) * t);
    const int g = bgr + (int)((fgr - bgr) * t);
    const int b = bb + (int)((fb - bb) * t);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

float uiAnimSlotT(uint32_t elapsed, uint8_t slot) {
    const int32_t local = (int32_t)elapsed - (int32_t)slot * UI_STAGGER_MS;
    if (local <= 0) return 0.0f;
    return uiAnimEase((float)local / (float)UI_ENTER_MS);
}

uint32_t uiAnimTotalMs(uint8_t slots) {
    return (uint32_t)UI_ENTER_MS + (uint32_t)UI_STAGGER_MS * slots;
}

bool uiAnimTouches(const UiBand& b, int y0, int y1) {
    return y1 >= b.y && y0 <= b.y + b.h;
}

// Arranca en true: el primer dibujado despues de encender no tiene nada
// coherente atras.
static bool s_invalidado = true;

// La pide el cambio de app y la consume el proximo revelado. Ver
// uiAnimCurtainOnce() en el header.
static bool s_cortinaPedida = false;

void uiAnimInvalidate() { s_invalidado = true; }

bool uiAnimBegin() {
    if (!s_ready) return false;

    const bool completo = s_invalidado;
    s_invalidado = false;
    // Una pantalla animada entra con su propia ola, que ya baja de arriba hacia
    // abajo: la cortina seria la misma cosa dos veces. Se consume la bandera
    // para que no quede armada esperando al proximo dibujo estatico.
    s_cortinaPedida = false;

    if (completo) {
        uiSprite.fillScreen(CANVAS);
        displayWaitVSync();
        uiSprite.pushSprite(0, 0);
        displayRecordPush(SCREEN_W * SCREEN_H * 2, 0);
    }
    s_animStart = millis();
    return completo;
}

void uiAnimClearBand(int y, int h) {
    if (!s_ready || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (h <= 0) return;
    uiSprite.fillRect(0, y, SCREEN_W, h, CANVAS);
    uiAnimPresent(y, h);
}

uint32_t uiAnimElapsed() { return millis() - s_animStart; }

// El panel refresca a 42 Hz (12 MHz de pclk sobre 548x518 con porches), o sea
// 23,6 ms por VSync. Empujar los 460 KB enteros no entra en ese presupuesto:
// medido daba 98,8 ms por frame, 10 fps.
void uiAnimPresent(int y, int h) {
    if (h <= 0) return;
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (h <= 0) return;

    displayWaitVSync();
    const uint32_t t0 = micros();
    uiSprite.setClipRect(0, y, SCREEN_W, h);
    tft.setClipRect(0, y, SCREEN_W, h);
    uiSprite.pushSprite(0, 0);
    tft.clearClipRect();
    uiSprite.clearClipRect();
    displayRecordPush(SCREEN_W * h * 2, micros() - t0);
}

// La cortina baja en bandas de este alto. 60 px cuestan ~33 ms —un VSync y
// medio— asi que el revelado entero son ocho frames y ~265 ms. Con bandas mas
// finas se ve mas suave pero se paga la constante de 23,4 ms una vez por banda
// y el barrido se hace largo.
static constexpr int CORTINA_H = 60;
// El filo que va bajando. Dos pixeles: uno solo se pierde en un panel que
// refresca a 42 Hz, y con tres empieza a parecer una barra.
static constexpr int FILO_H = 2;
static uint16_t s_filo[SCREEN_W * FILO_H];

void uiAnimCurtainOnce() { s_cortinaPedida = true; }

void uiAnimReveal() {
    if (!s_ready) return;

    const bool cortina = s_cortinaPedida;
    s_cortinaPedida = false;
    const uint32_t t0 = millis();

    if (!cortina) {
        displayWaitVSync();
        uiSprite.pushSprite(0, 0);
        displayRecordPush(SCREEN_W * SCREEN_H * 2, 0);
        s_invalidado = false;
        Serial.printf("[reveal] entero %lums\n", (unsigned long)(millis() - t0));
        return;
    }

    for (int y = 0; y < SCREEN_H; y += CORTINA_H) {
        const int h = (y + CORTINA_H > SCREEN_H) ? (SCREEN_H - y) : CORTINA_H;
        const int filoY = y + h - FILO_H;
        const bool llevaFilo = (y + h) < SCREEN_H;

        // El filo del paso anterior se guardo antes de pisarlo: se restaura acá
        // y entra en este mismo empuje, asi que nunca queda una raya de color
        // colgada en la pantalla.
        const int restaurar = y - FILO_H;
        if (y > 0) {
            uiSprite.pushImage(0, restaurar, SCREEN_W, FILO_H,
                               (const lgfx::rgb565_t*)s_filo);
        }

        if (llevaFilo) {
            // El cast va en los DOS lados. Con un uint16_t* pelado LovyanGFX
            // asume orden intercambiado —el de SPI— tanto al leer como al
            // escribir, asi que leer crudo y empujar como rgb565_t devolveria
            // el filo con los colores rotos, que es el mismo problema que tenian
            // las miniaturas del feed.
            uiSprite.readRect(0, filoY, SCREEN_W, FILO_H, (lgfx::rgb565_t*)s_filo);
            for (int x = 0; x < SCREEN_W; x++) {
                const uint16_t c = fercedSpectrum((float)x / (float)(SCREEN_W - 1));
                uiSprite.drawFastVLine(x, filoY, FILO_H, c);
            }
        }

        const int desde = (y > 0) ? restaurar : 0;
        uiAnimPresent(desde, y + h - desde);
    }
    s_invalidado = false;

    // La cortina no pasa por uiAnimCountFrame(), que mide la ola: son empujes
    // fuera de una transicion animada y falsearian esa media. Se mide aparte,
    // que es lo unico que contesta si el cambio de app entra en presupuesto.
    // Ocho bandas de 60 px deberian dar ~265 ms segun el modelo.
    const uint32_t total = millis() - t0;
    Serial.printf("[cortina] %d bandas de %d px, %lums, %lums por banda\n",
                  (SCREEN_H + CORTINA_H - 1) / CORTINA_H, CORTINA_H,
                  (unsigned long)total,
                  (unsigned long)(total / ((SCREEN_H + CORTINA_H - 1) / CORTINA_H)));
}

void uiAnimCountFrame(uint32_t t0) {
    const uint32_t frameUs = micros() - t0;
    s_frameCount++;
    s_frameSumUs += frameUs;
    if (frameUs > s_frameWorstUs) s_frameWorstUs = frameUs;

    if (s_lastFrameUs != 0) {
        const uint32_t periodUs = t0 - s_lastFrameUs;
        // Un hueco enorme no es un frame lento: es el loop bloqueado en otra
        // cosa. El refresco del feed hace HTTPS y puede tardar segundos, y si
        // cae en medio de una animacion se lleva puesta la media — se midio un
        // period de 1542 ms, o sea 0,6 fps, mientras la pantalla iba a 35.
        if (periodUs < MAX_PERIOD_US) {
            s_framePeriodSumUs += periodUs;
            s_framePeriodCount++;
        }
    }
    s_lastFrameUs = t0;
}

void uiAnimPublishStats() {
    if (s_frameCount > 0) {
        s_stats.frames = (uint16_t)s_frameCount;
        s_stats.avgUs100 = (uint16_t)((s_frameSumUs / s_frameCount) / 100);
        s_stats.worstUs100 = (uint16_t)(s_frameWorstUs / 100);
        s_stats.periodUs100 = s_framePeriodCount > 0
                                  ? (uint16_t)((s_framePeriodSumUs / s_framePeriodCount) / 100)
                                  : 0;

        // El mismo dato que viaja al proxy como [anim], pero por serie: medir
        // no puede depender de leer un log remoto. waitTimeouts delata si
        // displayWaitVSync() esta agotando su timeout en vez de sincronizar de
        // verdad con el panel.
        const DisplayDiagnostics d = displayGetDiagnostics();
        const float costMs = (float)s_frameSumUs / (float)s_frameCount / 1000.0f;
        const float periodMs = s_framePeriodCount > 0
                                   ? (float)s_framePeriodSumUs / (float)s_framePeriodCount / 1000.0f
                                   : 0.0f;
        Serial.printf(
            "[anim] frames=%lu cost=%.1fms max=%.1fms period=%.1fms "
            "fps=%.1f waits=%lu timeouts=%lu\n",
            (unsigned long)s_frameCount, costMs,
            (float)s_frameWorstUs / 1000.0f, periodMs,
            periodMs > 0.0f ? 1000.0f / periodMs : 0.0f,
            (unsigned long)d.waitCalls, (unsigned long)d.waitTimeouts);
    }
    s_frameCount = 0;
    s_frameSumUs = 0;
    s_frameWorstUs = 0;
    s_lastFrameUs = 0;
    s_framePeriodSumUs = 0;
    s_framePeriodCount = 0;
}

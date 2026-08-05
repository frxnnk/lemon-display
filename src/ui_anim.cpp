#include "ui_anim.h"

#include "config.h"

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

void uiAnimBegin() {
    if (!s_ready) return;
    uiSprite.fillScreen(CANVAS);
    displayWaitVSync();
    uiSprite.pushSprite(0, 0);
    displayRecordPush(SCREEN_W * SCREEN_H * 2, 0);
    s_animStart = millis();
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

#include "ui_v2_demo.h"

#include "config.h"

#if LEMON_V2_DEMO_MODE

#include <Arduino.h>
#include <qrcode.h>

#include "display_manager.h"
#include "v2_demo_timeline.h"
#include "data/PPNeueMachinaBold24.h"
#include "data/satoshi_fonts.h"
#include "data/lemon_v2_logo_black_120.h"
#include "data/lemon_v2_logo_light_120.h"


static constexpr uint16_t V2_BLACK = 0x1082;
static constexpr uint16_t V2_MAIN_GREEN = 0x06E3;
static constexpr uint16_t V2_LIME_YELLOW = 0xCFE6;
static constexpr int V2_SAFE_INSET = 48;
static constexpr int V2_SAFE_WIDTH = SCREEN_W - (V2_SAFE_INSET * 2);
static constexpr char V2_DEMO_DEEP_LINK[] =
    "https://lemon.me/acciones?utm_source=lemon_box_v2&utm_medium=device&utm_campaign=launch_lab_demo";

static LGFX_Sprite demoSpr(&tft);
static bool spriteReady = false;
static uint32_t demoStartMs = 0;
static uint32_t lastNowMs = 0;
static V2DemoScene lastScene = V2_DEMO_RETURN;
static int16_t lastStep = -1;

static void ensureSprite() {
    if (spriteReady) return;
    demoSpr.setPsram(true);
    demoSpr.setColorDepth(16);
    demoSpr.createSprite(SCREEN_W, SCREEN_H);
    spriteReady = true;
}

static void drawLogo(bool dark) {
    const uint16_t* logo = dark ? lemon_v2_logo_black_120 : lemon_v2_logo_light_120;
    for (int y = 0; y < 28; ++y) {
        for (int x = 0; x < 120; ++x) {
            demoSpr.drawPixel(V2_SAFE_INSET + x, V2_SAFE_INSET + y,
                              pgm_read_word(&logo[y * 120 + x]));
        }
    }
}

static void drawTag(bool dark) {
    const uint16_t color = dark ? V2_BLACK : V2_MAIN_GREEN;
    demoSpr.drawRoundRect(366, 48, 66, 30, 5, color);
    demoSpr.setTextDatum(lgfx::middle_center);
    demoSpr.setTextColor(color);
    demoSpr.drawString("DEMO", 399, 63, &Satoshi9);
}

static void drawHeader(bool lightBackground, bool showTag = true) {
    drawLogo(lightBackground);
    if (showTag) drawTag(lightBackground);
}

static void drawSparkline() {
    static const int points[][2] = {
        {48, 348}, {82, 339}, {110, 344}, {139, 322}, {174, 329}, {206, 306},
        {238, 315}, {270, 292}, {301, 304}, {332, 283}, {366, 291}, {397, 277}, {431, 281},
    };
    for (uint8_t i = 1; i < sizeof(points) / sizeof(points[0]); ++i) {
        demoSpr.drawLine(points[i - 1][0], points[i - 1][1],
                         points[i][0], points[i][1], V2_MAIN_GREEN);
        demoSpr.drawLine(points[i - 1][0], points[i - 1][1] + 1,
                         points[i][0], points[i][1] + 1, V2_MAIN_GREEN);
    }
}

static void drawHome(uint32_t countdownSeconds = 0) {
    demoSpr.fillSprite(V2_BLACK);
    drawHeader(false, false);
    demoSpr.setTextDatum(lgfx::top_right);
    demoSpr.setTextColor(V2_MAIN_GREEN);
    demoSpr.drawString("09:41", 432, 52, &Satoshi12);
    demoSpr.setTextDatum(lgfx::top_left);
    demoSpr.drawString("BITCOIN - DATOS DE DEMO", 48, 136, &Satoshi9);
    demoSpr.drawString("$ 118.420", 48, 164, &PPNeueMachinaBold24);
    demoSpr.drawString("+2,8% HOY", 48, 235, &SatoshiMedium18);
    drawSparkline();
    demoSpr.fillSmoothRoundRect(48, 380, 384, 52, 8, V2_MAIN_GREEN);
    demoSpr.setTextColor(V2_BLACK);
    demoSpr.setTextDatum(lgfx::middle_left);
    demoSpr.drawString("MERCADO EE.UU.", 64, 406, &Satoshi9);
    demoSpr.setTextDatum(lgfx::middle_right);
    if (countdownSeconds > 0) {
        char countdown[24];
        snprintf(countdown, sizeof(countdown), "ABRE EN 00:00:%02lu", (unsigned long)countdownSeconds);
        demoSpr.drawString(countdown, 416, 406, &Satoshi9);
    } else {
        demoSpr.drawString("ABRE EN 00:19:42", 416, 406, &Satoshi9);
    }
}

static void drawOpen(uint8_t step) {
    demoSpr.fillSprite(V2_BLACK);
    drawHeader(false);
    for (uint8_t i = 0; i < step && i < 6; ++i) {
        const int x = 62 + (i % 3) * 142;
        const int y = 132 + (i / 3) * 220;
        demoSpr.fillRect(x, y, 22, 22, V2_MAIN_GREEN);
    }
    demoSpr.setTextDatum(lgfx::middle_center);
    demoSpr.setTextColor(V2_MAIN_GREEN);
    demoSpr.drawString("MERCADO", SCREEN_W / 2, 205, &PPNeueMachinaBold24);
    demoSpr.drawString("ABIERTO", SCREEN_W / 2, 270, &PPNeueMachinaBold24);
}

static void drawTapeRow(int y, const char* symbol, const char* price,
                        const char* change, bool focus) {
    const uint16_t fg = focus ? V2_BLACK : V2_LIME_YELLOW;
    const uint16_t bg = focus ? V2_LIME_YELLOW : V2_BLACK;
    demoSpr.fillSmoothRoundRect(48, y, 384, 42, 6, bg);
    if (!focus) demoSpr.drawRoundRect(48, y, 384, 42, 6, fg);
    demoSpr.setTextColor(fg);
    demoSpr.setTextDatum(lgfx::middle_left);
    demoSpr.drawString(symbol, 60, y + 21, &Satoshi12);
    demoSpr.setTextDatum(lgfx::middle_right);
    demoSpr.drawString(price, 325, y + 21, &Satoshi9);
    demoSpr.drawString(change, 420, y + 21, &Satoshi9);
}

static void drawTape(uint8_t visibleRows, bool packFocus) {
    demoSpr.fillSprite(V2_BLACK);
    drawHeader(false);
    demoSpr.setTextDatum(lgfx::top_left);
    demoSpr.setTextColor(V2_LIME_YELLOW);
    demoSpr.drawString("MERCADO ABIERTO", 48, 112, &Satoshi9);
    demoSpr.drawString("MARKET TAPE", 48, 134, &PPNeueMachinaBold24);
    static const char* symbols[] = { "S&P 500", "NASDAQ", "AAPL", "NVDA" };
    static const char* prices[] = { "6.309,62", "21.083,32", "227,16", "171,38" };
    static const char* changes[] = { "+0,54%", "+0,61%", "+1,80%", "-2,10%" };
    for (uint8_t i = 0; i < visibleRows && i < 4; ++i) {
        drawTapeRow(205 + i * 49, symbols[i], prices[i], changes[i], i == 0 && !packFocus);
    }
    if (packFocus) {
        demoSpr.fillSmoothRoundRect(48, 401, 384, 31, 5, V2_LIME_YELLOW);
        demoSpr.setTextColor(V2_BLACK);
        demoSpr.setTextDatum(lgfx::middle_left);
        demoSpr.drawString("PACK IA", 64, 416, &Satoshi12);
        demoSpr.setTextDatum(lgfx::middle_right);
        demoSpr.drawString("+1,4%", 416, 416, &Satoshi12);
    }
}

static void drawNews(bool context) {
    demoSpr.fillSprite(V2_LIME_YELLOW);
    drawHeader(true);
    demoSpr.setTextColor(V2_BLACK);
    demoSpr.setTextDatum(lgfx::top_left);
    demoSpr.drawString(context ? "POR QUE IMPORTA" : "QUE PASO", 48, 114, &Satoshi9);
    if (!context) {
        demoSpr.drawString("NVIDIA", 48, 140, &PPNeueMachinaBold24);
        demoSpr.drawString("CAE 4,2%", 48, 204, &PPNeueMachinaBold24);
        demoSpr.drawString("Nuevas restricciones de exportacion", 48, 282, &Satoshi12);
        demoSpr.drawString("presionan al sector de chips.", 48, 308, &Satoshi12);
        demoSpr.drawFastHLine(48, 348, 384, V2_BLACK);
        demoSpr.drawString("TOCA PARA ENTENDER", 48, 365, &Satoshi9);
    } else {
        demoSpr.drawString("PUEDE MOVER", 48, 146, &PPNeueMachinaBold24);
        demoSpr.drawString("TECNOLOGIA", 48, 210, &PPNeueMachinaBold24);
        demoSpr.drawString("- Cambian expectativas de ventas.", 48, 286, &Satoshi12);
        demoSpr.drawString("- Impacta al sector y al Pack IA.", 48, 318, &Satoshi12);
        demoSpr.drawString("FUENTE DE DEMO - 09:41", 48, 369, &Satoshi9);
    }
    demoSpr.fillSmoothRoundRect(48, 386, 384, 46, 7, V2_BLACK);
    demoSpr.setTextColor(V2_LIME_YELLOW);
    demoSpr.setTextDatum(lgfx::middle_left);
    demoSpr.drawString(context ? "VER EN LEMON" : "VER CONTEXTO", 64, 409, &Satoshi12);
    demoSpr.setTextDatum(lgfx::middle_right);
    demoSpr.drawString(">", 416, 409, &SatoshiBold24);
}

static void drawQr() {
    demoSpr.fillSprite(V2_BLACK);
    drawHeader(false);
    demoSpr.setTextColor(V2_LIME_YELLOW);
    demoSpr.setTextDatum(lgfx::top_center);
    demoSpr.drawString("SEGUI EN LEMON", SCREEN_W / 2, 104, &SatoshiMedium18);
    QRCode qr;
    uint8_t data[512];
    qrcode_initText(&qr, data, 6, ECC_LOW, V2_DEMO_DEEP_LINK);
    const int scale = 5;
    const int quiet = 4;
    const int size = (qr.size + quiet * 2) * scale;
    const int x0 = (SCREEN_W - size) / 2;
    const int y0 = 145;
    demoSpr.fillRect(x0, y0, size, size, V2_LIME_YELLOW);
    for (uint8_t y = 0; y < qr.size; ++y) {
        for (uint8_t x = 0; x < qr.size; ++x) {
            if (qrcode_getModule(&qr, x, y)) {
                demoSpr.fillRect(x0 + (x + quiet) * scale, y0 + (y + quiet) * scale,
                                 scale, scale, V2_BLACK);
            }
        }
    }
    demoSpr.setTextDatum(lgfx::bottom_center);
    demoSpr.drawString("LINK DE DEMO", SCREEN_W / 2, 438, &Satoshi9);
}

static void renderFrame(const V2DemoFrame& frame, int16_t step) {
    switch (frame.scene) {
        case V2_DEMO_HOME: drawHome(); break;
        case V2_DEMO_PREOPEN: drawHome(7 - step); break;
        case V2_DEMO_OPEN: drawOpen(step + 1); break;
        case V2_DEMO_TAPE: drawTape(step + 1, false); break;
        case V2_DEMO_PACK: drawTape(4, true); break;
        case V2_DEMO_NEWS: drawNews(false); break;
        case V2_DEMO_CONTEXT: drawNews(true); break;
        case V2_DEMO_QR: drawQr(); break;
        case V2_DEMO_RETURN: drawHome(); break;
    }
    displayWaitVSync();
    demoSpr.pushSprite(0, 0);
}

static int16_t frameStep(const V2DemoFrame& frame) {
    if (frame.scene == V2_DEMO_PREOPEN) return frame.sceneElapsedMs / 1000;
    if (frame.scene == V2_DEMO_OPEN) return frame.sceneElapsedMs / 650;
    if (frame.scene == V2_DEMO_TAPE) return min<uint32_t>(3, frame.sceneElapsedMs / 2500);
    return 0;
}

void v2DemoSetup() {
    ensureSprite();
    demoStartMs = millis();
    lastNowMs = demoStartMs;
    lastScene = V2_DEMO_RETURN;
    lastStep = -1;
    v2DemoTick(demoStartMs);
}

void v2DemoTick(uint32_t nowMs) {
    lastNowMs = nowMs;
    const V2DemoFrame frame = v2DemoFrameAt(nowMs - demoStartMs);
    const int16_t step = frameStep(frame);
    if (frame.scene == lastScene && step == lastStep) return;
    lastScene = frame.scene;
    lastStep = step;
    renderFrame(frame, step);
}

void v2DemoHandleTouch(const TouchEvent& event) {
    if (event.gesture != TOUCH_TAP) return;
    V2DemoScene target = V2_DEMO_HOME;
    if (lastScene == V2_DEMO_NEWS) target = V2_DEMO_CONTEXT;
    else if (lastScene == V2_DEMO_CONTEXT) target = V2_DEMO_QR;
    else if (lastScene != V2_DEMO_QR) return;
    demoStartMs = lastNowMs - v2DemoSceneStartMs(target);
    lastScene = V2_DEMO_RETURN;
    lastStep = -1;
    v2DemoTick(lastNowMs);
}

#endif

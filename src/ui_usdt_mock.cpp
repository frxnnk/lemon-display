#include "ui_usdt_mock.h"

#include "config.h"

#if LEMON_USDT_MOCK_MODE

#include "display_manager.h"
#include "data/satoshi_fonts.h"
#include "data/lemon_v2_logo_light_120.h"

namespace {
constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t GREEN = 0x06E3;
constexpr uint16_t MUTED = 0x7BEF;
constexpr uint16_t PANEL = 0x18E3;

enum MockScene : uint8_t {
    OVERVIEW,
    NETWORKS,
    MARKETS,
    REGIONS,
    INTEL,
    ALERT,
    SCENE_COUNT,
};

LGFX_Sprite sprite(&tft);
bool ready = false;
MockScene scene = OVERVIEW;
MockScene drawnScene = SCENE_COUNT;

void ensureSprite() {
    if (ready) return;
    sprite.setPsram(true);
    sprite.setColorDepth(16);
    sprite.createSprite(SCREEN_W, SCREEN_H);
    ready = true;
}

void drawLogo() {
    for (int y = 0; y < 28; ++y) {
        for (int x = 0; x < 120; ++x) {
            sprite.drawPixel(28 + x, 28 + y,
                             pgm_read_word(&lemon_v2_logo_light_120[y * 120 + x]));
        }
    }
}

void text(const char* value, int x, int y, const GFXfont* font,
          uint16_t color = WHITE, lgfx::textdatum_t datum = lgfx::top_left) {
    sprite.setTextDatum(datum);
    sprite.setTextColor(color, BLACK);
    sprite.drawString(value, x, y, font);
}

void header(const char* title) {
    sprite.fillSprite(BLACK);
    drawLogo();
    text("USDT CONTROL ROOM", 28, 78, &SatoshiBold24, WHITE);
    text(title, 28, 114, &Satoshi9, GREEN);
    text("09:41", 452, 42, &Satoshi12, GREEN, lgfx::top_right);
    sprite.fillRect(28, 138, 424, 1, PANEL);
}

void card(int x, int y, int w, int h, const char* label, const char* value,
          const char* suffix = nullptr) {
    sprite.drawRoundRect(x, y, w, h, 6, PANEL);
    text(label, x + 14, y + 12, &Satoshi12, GREEN);
    text(value, x + 14, y + 42, &SatoshiBold24, WHITE);
    if (suffix) text(suffix, x + w - 14, y + h - 16, &Satoshi9, GREEN, lgfx::bottom_right);
}

void tabs() {
    static const char* labels[] = { "OVERVIEW", "NETWORKS", "MARKETS", "REGIONS", "INTEL", "ALERT" };
    const int width = SCREEN_W / 6;
    sprite.fillRect(0, 430, SCREEN_W, 50, BLACK);
    sprite.fillRect(scene * width, 430, width, 3, GREEN);
    for (uint8_t i = 0; i < SCENE_COUNT; ++i) {
        text(labels[i], i * width + width / 2, 448, &Satoshi9,
             i == scene ? GREEN : MUTED, lgfx::middle_center);
    }
}

void drawOverview() {
    header("SYSTEM HEALTHY  •  LIVE MOCK");
    text("SYSTEM HEALTHY", 28, 160, &SatoshiBold40, GREEN);
    card(28, 224, 204, 78, "PEG", "0.9998", "USD");
    card(248, 224, 204, 78, "SUPPLY", "186.4B");
    card(28, 316, 204, 78, "24H VOL", "92.7B");
    card(248, 316, 204, 78, "LEMON", "1 USDT", "= 1.342 ARS");
}

void drawNetworks() {
    header("NETWORKS  •  SETTLEMENT RAILS");
    text("NETWORKS", 28, 160, &SatoshiBold40, GREEN);
    card(28, 224, 204, 78, "TRON", "ACTIVE", "LOW FEES");
    card(248, 224, 204, 78, "ETHEREUM", "ACTIVE", "DEEP LIQUIDITY");
    card(28, 316, 204, 78, "SOLANA", "READY", "FAST");
    card(248, 316, 204, 78, "RESERVES", "VERIFIED", "MOCK DATA");
}

void drawMarkets() {
    header("MARKETS  •  PRICE DISCOVERY");
    text("MARKETS", 28, 160, &SatoshiBold40, GREEN);
    card(28, 224, 204, 78, "USDT / ARS", "1.342", "+0.18%");
    card(248, 224, 204, 78, "USDT / USD", "0.9998", "-0.02%");
    card(28, 316, 204, 78, "BTC / USDT", "118,420", "+2.8%");
    card(248, 316, 204, 78, "SPREAD", "0.12%", "HEALTHY");
}

void drawRegions() {
    header("REGIONS  •  LOCAL SIGNAL");
    text("REGIONS", 28, 160, &SatoshiBold40, GREEN);
    card(28, 224, 204, 78, "ARGENTINA", "1.342", "ARS");
    card(248, 224, 204, 78, "BRAZIL", "5.48", "BRL");
    card(28, 316, 204, 78, "MEXICO", "18.72", "MXN");
    card(248, 316, 204, 78, "LATAM FLOW", "+4.2%", "24H MOCK");
}

void drawIntel() {
    header("INTEL  •  DECISION CONTEXT");
    text("INTEL", 28, 160, &SatoshiBold40, GREEN);
    card(28, 224, 204, 78, "LIQUIDITY", "DEEP", "CONFIDENCE 92%");
    card(248, 224, 204, 78, "PEG RISK", "LOW", "0.4 / 10");
    card(28, 316, 204, 78, "FLOW", "INBOUND", "TRENDING");
    card(248, 316, 204, 78, "SOURCE", "MOCK", "VALIDATION BUILD");
}

void drawAlert() {
    header("ALERT  •  OPERATIONS");
    text("ALERT", 28, 160, &SatoshiBold40, GREEN);
    card(28, 224, 424, 78, "STATUS", "ALL SYSTEMS CLEAR", "NO LIVE ACTION");
    card(28, 316, 204, 78, "LAST SYNC", "09:41", "MOCK");
    card(248, 316, 204, 78, "OTA", "READY", "GITHUB RELEASE");
}

void render() {
    switch (scene) {
        case OVERVIEW: drawOverview(); break;
        case NETWORKS: drawNetworks(); break;
        case MARKETS: drawMarkets(); break;
        case REGIONS: drawRegions(); break;
        case INTEL: drawIntel(); break;
        case ALERT: drawAlert(); break;
        default: scene = OVERVIEW; drawOverview(); break;
    }
    tabs();
    displayWaitVSync();
    sprite.pushSprite(0, 0);
}
}

void usdtMockSetup() {
    ensureSprite();
    drawnScene = SCENE_COUNT;
    render();
    drawnScene = scene;
}

void usdtMockTick(uint32_t) {
    if (scene == drawnScene) return;
    render();
    drawnScene = scene;
}

void usdtMockHandleTouch(const TouchEvent& event) {
    if (event.gesture != TOUCH_TAP && event.gesture != TOUCH_SWIPE_LEFT &&
        event.gesture != TOUCH_SWIPE_RIGHT) return;
    if (event.gesture == TOUCH_SWIPE_LEFT ||
        (event.gesture == TOUCH_TAP && event.x > SCREEN_W / 2)) {
        scene = static_cast<MockScene>((scene + 1) % SCENE_COUNT);
    } else if (event.gesture == TOUCH_SWIPE_RIGHT || event.gesture == TOUCH_TAP) {
        scene = static_cast<MockScene>((scene + SCENE_COUNT - 1) % SCENE_COUNT);
    }
}

#endif

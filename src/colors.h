#pragma once

#include <cstdint>

namespace Colors {
    enum UiTheme : uint8_t {
        THEME_DARK = 0,
        THEME_LIGHT = 1,
    };

    struct ThemePalette {
        uint16_t bgBase;
        uint16_t bgCard;
        uint16_t bgSurface;
        uint16_t bgElevated;
        uint16_t bgOverlay;
        uint16_t bgInput;
        uint16_t lemonGreen;
        uint16_t darkGreen;
        uint16_t nebula;
        uint16_t solar;
        uint16_t starlight;
        uint16_t moon;
        uint16_t greenDim;
        uint16_t nebulaFill;
        uint16_t coinBtc;
        uint16_t coinEth;
        uint16_t coinSol;
        uint16_t coinUsdt;
        uint16_t coinUsdc;
        uint16_t textPrimary;
        uint16_t textSecondary;
        uint16_t textTertiary;
        uint16_t textDisabled;
        uint16_t textLight;
        uint16_t positive;
        uint16_t negative;
        uint16_t badgeBgPos;
        uint16_t badgeBgNeg;
        uint16_t badgeBg;
        uint16_t chartLine;
        uint16_t chartFill;
        uint16_t chartGrid;
        uint16_t candleBull;
        uint16_t candleBear;
        uint16_t candleWick;
        uint16_t candleBullGlow;
        uint16_t candleBearGlow;
        uint16_t candleBullHl;
        uint16_t candleBearHl;
        uint16_t markerHigh;
        uint16_t markerLow;
        uint16_t athLine;
        uint16_t domBtc;
        uint16_t domEth;
        uint16_t domOther;
        uint16_t cardBorder;
        uint16_t cardBorderAccent;
        uint16_t cardHighlight;
        uint16_t divider;
        uint16_t polyYesBg;
        uint16_t polyNoBg;
        uint16_t glow1;
        uint16_t glow2;
        uint16_t glow3;
        uint16_t glow4;
    };

    extern uint16_t BG_BASE;
    extern uint16_t BG_CARD;
    extern uint16_t BG_SURFACE;
    extern uint16_t BG_ELEVATED;
    extern uint16_t BG_OVERLAY;
    extern uint16_t BG_INPUT;

    extern uint16_t LEMON_GREEN;
    extern uint16_t DARK_GREEN;
    extern uint16_t NEBULA;
    extern uint16_t SOLAR;
    extern uint16_t STARLIGHT;
    extern uint16_t MOON;
    extern uint16_t GREEN_DIM;
    extern uint16_t NEBULA_FILL;

    extern uint16_t COIN_BTC;
    extern uint16_t COIN_ETH;
    extern uint16_t COIN_SOL;
    extern uint16_t COIN_USDT;
    extern uint16_t COIN_USDC;

    extern uint16_t TEXT_PRIMARY;
    extern uint16_t TEXT_SECONDARY;
    extern uint16_t TEXT_TERTIARY;
    extern uint16_t TEXT_DISABLED;
    extern uint16_t TEXT_LIGHT;

    extern uint16_t POSITIVE;
    extern uint16_t NEGATIVE;
    extern uint16_t PRICE_UP;
    extern uint16_t PRICE_DOWN;

    extern uint16_t BADGE_BG_POS;
    extern uint16_t BADGE_BG_NEG;
    extern uint16_t BADGE_BG;

    extern uint16_t CHART_LINE;
    extern uint16_t CHART_FILL;
    extern uint16_t CHART_GRID;

    extern uint16_t CANDLE_BULL;
    extern uint16_t CANDLE_BEAR;
    extern uint16_t CANDLE_WICK;
    extern uint16_t CANDLE_BULL_GLOW;
    extern uint16_t CANDLE_BEAR_GLOW;
    extern uint16_t CANDLE_BULL_HL;
    extern uint16_t CANDLE_BEAR_HL;

    extern uint16_t MARKER_HIGH;
    extern uint16_t MARKER_LOW;
    extern uint16_t ATH_LINE;

    extern uint16_t DOM_BTC;
    extern uint16_t DOM_ETH;
    extern uint16_t DOM_OTHER;

    extern uint16_t CARD_BORDER;
    extern uint16_t CARD_BORDER_ACCENT;
    extern uint16_t CARD_HIGHLIGHT;
    extern uint16_t DIVIDER;

    extern uint16_t POLY_YES_BG;
    extern uint16_t POLY_NO_BG;

    extern uint16_t GLOW_1;
    extern uint16_t GLOW_2;
    extern uint16_t GLOW_3;
    extern uint16_t GLOW_4;

    void setTheme(UiTheme theme);
    UiTheme getTheme();
    bool isLightTheme();
    const char* themeLabel(UiTheme theme);
}

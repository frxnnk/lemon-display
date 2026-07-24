#include "colors.h"

namespace Colors {
    uint16_t BG_BASE;
    uint16_t BG_CARD;
    uint16_t BG_SURFACE;
    uint16_t BG_ELEVATED;
    uint16_t BG_OVERLAY;
    uint16_t BG_INPUT;

    uint16_t LEMON_GREEN;
    uint16_t DARK_GREEN;
    uint16_t NEBULA;
    uint16_t SOLAR;
    uint16_t STARLIGHT;
    uint16_t MOON;
    uint16_t GREEN_DIM;
    uint16_t NEBULA_FILL;

    uint16_t COIN_BTC;
    uint16_t COIN_ETH;
    uint16_t COIN_SOL;
    uint16_t COIN_USDT;
    uint16_t COIN_USDC;

    uint16_t TEXT_PRIMARY;
    uint16_t TEXT_SECONDARY;
    uint16_t TEXT_TERTIARY;
    uint16_t TEXT_DISABLED;
    uint16_t TEXT_LIGHT;

    uint16_t POSITIVE;
    uint16_t NEGATIVE;
    uint16_t PRICE_UP;
    uint16_t PRICE_DOWN;

    uint16_t BADGE_BG_POS;
    uint16_t BADGE_BG_NEG;
    uint16_t BADGE_BG;

    uint16_t CHART_LINE;
    uint16_t CHART_FILL;
    uint16_t CHART_GRID;

    uint16_t CANDLE_BULL;
    uint16_t CANDLE_BEAR;
    uint16_t CANDLE_WICK;
    uint16_t CANDLE_BULL_GLOW;
    uint16_t CANDLE_BEAR_GLOW;
    uint16_t CANDLE_BULL_HL;
    uint16_t CANDLE_BEAR_HL;

    uint16_t MARKER_HIGH;
    uint16_t MARKER_LOW;
    uint16_t ATH_LINE;

    uint16_t DOM_BTC;
    uint16_t DOM_ETH;
    uint16_t DOM_OTHER;

    uint16_t CARD_BORDER;
    uint16_t CARD_BORDER_ACCENT;
    uint16_t CARD_HIGHLIGHT;
    uint16_t DIVIDER;

    uint16_t POLY_YES_BG;
    uint16_t POLY_NO_BG;

    uint16_t GLOW_1;
    uint16_t GLOW_2;
    uint16_t GLOW_3;
    uint16_t GLOW_4;

    static UiTheme activeTheme = THEME_DARK;

    static const ThemePalette darkPalette = {
        0x0000, 0x1082, 0x2104, 0x3186, 0x2104, 0x4208,
        0x078D, 0x0549, 0x837E, 0xFC20, 0xE73C, 0x5ACB, 0x0360, 0x2018,
        0xFC20, 0x4C7F, 0x837E, 0x0549, 0x2D7F,
        0xFFFF, 0xBDF7, 0x8C71, 0x632C, 0xFFFF,
        0x078D, 0xF8E6,
        0x0220, 0x3000, 0x18C3,
        0x078D, 0x01C3, 0x39E7,
        0x078D, 0xF8E6, 0x5ACB, 0x0220, 0x3000, 0x5FED, 0xFCB3,
        0x078D, 0xF8E6, 0xFC20,
        0xFC20, 0x837E, 0x5ACB,
        0x52AA, 0x078D, 0x4208, 0x39E7,
        0x0220, 0x3000,
        0x0120, 0x01A0, 0x0240, 0x0360,
    };

    static const ThemePalette lightPalette = {
        0xF7DE, 0xFFFF, 0xEFBD, 0xDF5B, 0xFFFF, 0xE77D,
        0x078D, 0xDFDD, 0x837E, 0xFC20, 0xFFFF, 0x7C2F, 0xC7BB, 0xF77F,
        0xFC20, 0x4C7F, 0x837E, 0x0549, 0x2D7F,
        0x10A2, 0x4ACA, 0x7C2F, 0xBE17, 0xFFFF,
        0x0549, 0xE127,
        0xDFDD, 0xFF3D, 0xF79D,
        0x0549, 0xDFDD, 0xCEB9,
        0x0549, 0xE127, 0x7C2F, 0xC7BB, 0xFEFC, 0x67B4, 0xFCD3,
        0x0549, 0xE127, 0xFC20,
        0xFC20, 0x837E, 0x7C2F,
        0xBE57, 0x9DD4, 0xE77C, 0xCEB9,
        0xDFDD, 0xFF3D,
        0xC7BB, 0xDFDD, 0xEF7D, 0xFFFF,
    };

    static void applyPalette(const ThemePalette& p) {
        BG_BASE = p.bgBase;
        BG_CARD = p.bgCard;
        BG_SURFACE = p.bgSurface;
        BG_ELEVATED = p.bgElevated;
        BG_OVERLAY = p.bgOverlay;
        BG_INPUT = p.bgInput;

        LEMON_GREEN = p.lemonGreen;
        DARK_GREEN = p.darkGreen;
        NEBULA = p.nebula;
        SOLAR = p.solar;
        STARLIGHT = p.starlight;
        MOON = p.moon;
        GREEN_DIM = p.greenDim;
        NEBULA_FILL = p.nebulaFill;

        COIN_BTC = p.coinBtc;
        COIN_ETH = p.coinEth;
        COIN_SOL = p.coinSol;
        COIN_USDT = p.coinUsdt;
        COIN_USDC = p.coinUsdc;

        TEXT_PRIMARY = p.textPrimary;
        TEXT_SECONDARY = p.textSecondary;
        TEXT_TERTIARY = p.textTertiary;
        TEXT_DISABLED = p.textDisabled;
        TEXT_LIGHT = p.textLight;

        POSITIVE = p.positive;
        NEGATIVE = p.negative;
        PRICE_UP = POSITIVE;
        PRICE_DOWN = NEGATIVE;

        BADGE_BG_POS = p.badgeBgPos;
        BADGE_BG_NEG = p.badgeBgNeg;
        BADGE_BG = p.badgeBg;

        CHART_LINE = p.chartLine;
        CHART_FILL = p.chartFill;
        CHART_GRID = p.chartGrid;

        CANDLE_BULL = p.candleBull;
        CANDLE_BEAR = p.candleBear;
        CANDLE_WICK = p.candleWick;
        CANDLE_BULL_GLOW = p.candleBullGlow;
        CANDLE_BEAR_GLOW = p.candleBearGlow;
        CANDLE_BULL_HL = p.candleBullHl;
        CANDLE_BEAR_HL = p.candleBearHl;

        MARKER_HIGH = p.markerHigh;
        MARKER_LOW = p.markerLow;
        ATH_LINE = p.athLine;

        DOM_BTC = p.domBtc;
        DOM_ETH = p.domEth;
        DOM_OTHER = p.domOther;

        CARD_BORDER = p.cardBorder;
        CARD_BORDER_ACCENT = p.cardBorderAccent;
        CARD_HIGHLIGHT = p.cardHighlight;
        DIVIDER = p.divider;

        POLY_YES_BG = p.polyYesBg;
        POLY_NO_BG = p.polyNoBg;

        GLOW_1 = p.glow1;
        GLOW_2 = p.glow2;
        GLOW_3 = p.glow3;
        GLOW_4 = p.glow4;
    }

    void setTheme(UiTheme theme) {
        activeTheme = (theme == THEME_LIGHT) ? THEME_LIGHT : THEME_DARK;
        applyPalette(activeTheme == THEME_LIGHT ? lightPalette : darkPalette);
    }

    UiTheme getTheme() {
        return activeTheme;
    }

    bool isLightTheme() {
        return activeTheme == THEME_LIGHT;
    }

    const char* themeLabel(UiTheme theme) {
        return (theme == THEME_LIGHT) ? "Claro" : "Oscuro";
    }
}

namespace {
    struct ThemeBootstrap {
        ThemeBootstrap() {
            Colors::setTheme(Colors::THEME_DARK);
        }
    };

    ThemeBootstrap themeBootstrap;
}

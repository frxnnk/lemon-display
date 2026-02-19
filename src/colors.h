#pragma once

#include <cstdint>

// Lemon brand colors (RGB565) — v3.1 palette (crypto-focused)
// Brand: Black 50%, Greent 25%, Nebula 10%, Solar 5%, Starlight 5%, Moon 5%
namespace Colors {
    // ── Backgrounds (dark theme — green tinted) ──
    constexpr uint16_t BG_BASE       = 0x0000;  // #000000  Pure black (50%)
    constexpr uint16_t BG_CARD       = 0x0841;  // #080C08  Card fill (green tint)
    constexpr uint16_t BG_SURFACE    = 0x18C3;  // #1A1A1A  Elevated surface
    constexpr uint16_t BG_ELEVATED   = 0x2945;  // #2A2A2A  Active states
    constexpr uint16_t BG_OVERLAY    = 0x18E3;  // #1E1E1E  Header/footer
    constexpr uint16_t BG_INPUT      = 0x39C7;  // #3E3E3E  Input field bg

    // ── Brand (corrected RGB565 from official PDF p20) ──
    constexpr uint16_t LEMON_GREEN  = 0x078D;  // #00F068  Greent — primary accent (25%)
    constexpr uint16_t DARK_GREEN   = 0x0549;  // #00A849  Evergreent — dark bg variant
    constexpr uint16_t NEBULA       = 0x837E;  // #806CF2  Nebula — secondary accent (10%)
    constexpr uint16_t SOLAR        = 0xFC20;  // #FF8700  Solar — BTC accent (5%)
    constexpr uint16_t STARLIGHT    = 0xE73C;  // #E7E7E7  Starlight — light text (5%)
    constexpr uint16_t MOON         = 0x5ACB;  // #5B5B5B  Moon — muted surfaces (5%)
    constexpr uint16_t GREEN_DIM    = 0x0360;  // #006030  Muted green for subtle accents
    constexpr uint16_t NEBULA_FILL  = 0x2018;  // #200830  Nebula gradient fill (purple→BG)

    // ── Coin accent colors ──
    constexpr uint16_t COIN_BTC     = 0xFC20;  // #FF8700  Solar (same as SOLAR)
    constexpr uint16_t COIN_ETH     = 0x4C7F;  // #4A90D9  Ethereum blue
    constexpr uint16_t COIN_SOL     = 0x837E;  // #806CF2  Nebula (same as NEBULA)
    constexpr uint16_t COIN_USDT    = 0x0549;  // #00A849  Evergreent (same as DARK_GREEN)
    constexpr uint16_t COIN_USDC    = 0x2D7F;  // #2775CA  USDC blue

    // ── Text ──
    constexpr uint16_t TEXT_PRIMARY   = 0xFFFF;  // #FFFFFF
    constexpr uint16_t TEXT_SECONDARY = 0x8410;  // #868686
    constexpr uint16_t TEXT_TERTIARY  = 0x5AEB;  // #5B5B5B
    constexpr uint16_t TEXT_DISABLED  = 0x39C7;  // #3E3E3E
    constexpr uint16_t TEXT_LIGHT     = 0xE73C;  // #E7E7E7  (same as STARLIGHT)

    // ── Status ──
    constexpr uint16_t POSITIVE    = 0x078D;  // Same as LEMON_GREEN
    constexpr uint16_t NEGATIVE    = 0xF8E6;  // #FF1A3B  Vivid red
    constexpr uint16_t PRICE_UP    = POSITIVE;
    constexpr uint16_t PRICE_DOWN  = NEGATIVE;

    // ── Badge backgrounds (tinted) ──
    constexpr uint16_t BADGE_BG_POS  = 0x0220;  // #003010  Dark green tint
    constexpr uint16_t BADGE_BG_NEG  = 0x3000;  // #300008  Dark red tint
    constexpr uint16_t BADGE_BG      = 0x18C3;  // #1A1A1A  Neutral badge

    // ── Chart ──
    constexpr uint16_t CHART_LINE = 0x078D;  // Green sparkline stroke (LEMON_GREEN)
    constexpr uint16_t CHART_FILL = 0x01C3;  // #003818  Dark green fill
    constexpr uint16_t CHART_GRID = 0x18C3;  // Subtle grid

    // Candlestick colors (glassmorphism style)
    constexpr uint16_t CANDLE_BULL      = POSITIVE;    // Green for bullish
    constexpr uint16_t CANDLE_BEAR      = NEGATIVE;    // Red for bearish
    constexpr uint16_t CANDLE_WICK      = 0x5ACB;      // MOON gray for wicks
    constexpr uint16_t CANDLE_BULL_GLOW = 0x0220;      // #003010  Dim green outer glow
    constexpr uint16_t CANDLE_BEAR_GLOW = 0x3000;      // #300008  Dim red outer glow
    constexpr uint16_t CANDLE_BULL_HL   = 0x5FED;      // #5BFFA8  Bright green center highlight
    constexpr uint16_t CANDLE_BEAR_HL   = 0xFCB3;      // #FF9898  Bright red center highlight

    // Marker colors
    constexpr uint16_t MARKER_HIGH = POSITIVE;     // Green for period high
    constexpr uint16_t MARKER_LOW  = NEGATIVE;     // Red for period low
    constexpr uint16_t ATH_LINE    = SOLAR;        // Orange for ATH

    // ── Dominance ──
    constexpr uint16_t DOM_BTC   = 0xFC20;  // SOLAR orange for BTC
    constexpr uint16_t DOM_ETH   = 0x837E;  // NEBULA purple for ETH
    constexpr uint16_t DOM_OTHER = 0x5ACB;  // MOON — muted gray

    // ── UI Elements ──
    constexpr uint16_t CARD_BORDER       = 0x2965;  // #2A2C2A  Subtle border (green tint)
    constexpr uint16_t CARD_BORDER_ACCENT = 0x0360; // #006030  Accent border for hero
    constexpr uint16_t CARD_HIGHLIGHT    = 0x2965;  // Top highlight line (subtle)
    constexpr uint16_t DIVIDER           = 0x2104;  // #222222 Horizontal dividers

    // ── Polymarket prediction mode ──
    constexpr uint16_t POLY_YES_BG = BADGE_BG_POS;   // Dark green tint
    constexpr uint16_t POLY_NO_BG  = BADGE_BG_NEG;   // Dark red tint

    // ── Price glow (concentric circles behind main price) ──
    constexpr uint16_t GLOW_1 = 0x0120;  // Darkest green ring
    constexpr uint16_t GLOW_2 = 0x01A0;  // Mid-dark
    constexpr uint16_t GLOW_3 = 0x0240;  // Mid
    constexpr uint16_t GLOW_4 = 0x0360;  // Brightest ring (still subtle)
}

#pragma once

// ══════════════════════════════════════════════
//  Lemon Design System v2.0
//  Mapping lemon.me brand to ESP32 LovyanGFX
// ══════════════════════════════════════════════

#include <LovyanGFX.hpp>
#include "data/satoshi_fonts.h"

namespace DS {

    // ── Typography (Satoshi — Lemon brand secondary typeface) ──
    inline const lgfx::IFont* fontHero()     { return (const lgfx::IFont*)&SatoshiBold40; }   // Main price display
    inline const lgfx::IFont* fontDataLg()   { return (const lgfx::IFont*)&SatoshiBold24; }   // Large data (BTC price)
    inline const lgfx::IFont* fontDisplay()  { return (const lgfx::IFont*)&SatoshiBold24; }   // Large titles
    inline const lgfx::IFont* fontHeading()  { return (const lgfx::IFont*)&SatoshiMedium18; } // Section titles
    inline const lgfx::IFont* fontBody()     { return (const lgfx::IFont*)&Satoshi12; }       // Body text
    inline const lgfx::IFont* fontCaption()  { return (const lgfx::IFont*)&Satoshi9; }        // Captions, labels
    inline const lgfx::IFont* fontData()     { return (const lgfx::IFont*)&SatoshiMedium18; } // Numeric data
    inline const lgfx::IFont* fontClock()    { return &fonts::Orbitron_Light_24; }             // Clock display
    inline const lgfx::IFont* fontMono()     { return &fonts::Font2; }                         // Status/loading text
    inline const lgfx::IFont* fontButton()   { return (const lgfx::IFont*)&SatoshiMedium18; } // Button labels

    // ── Escala de Ferced ──
    // El aparato se mira de reojo desde un metro o dos, no de cerca como una
    // web. fontHeading() (12pt) alcanza para una etiqueta pero deja un titular
    // leyendose como parrafo, asi que el titular tiene su propio tamano.
    inline const lgfx::IFont* fontTitular()  { return (const lgfx::IFont*)&SatoshiMedium28; } // 19pt — titulares

    // La cursiva con gracias es el gesto que ferced.com repite en cada seccion:
    // una palabra en serif italica dentro de un titular en Satoshi. Aca cumple
    // la misma funcion —marcar lo que no es dato duro— en los accesorios: la
    // hora relativa, el numero de un torneo, un estado vacio.
    inline const lgfx::IFont* fontAcento()   { return (const lgfx::IFont*)&GeorgiaItalic28; } // acento grande
    inline const lgfx::IFont* fontAcentoSm() { return (const lgfx::IFont*)&GeorgiaItalic16; } // acento chico

    // ── Spacing Scale (px) ──
    constexpr int S4   =  4;
    constexpr int S8   =  8;
    constexpr int S12  = 12;
    constexpr int S16  = 16;
    constexpr int S20  = 20;
    constexpr int S24  = 24;
    constexpr int S32  = 32;
    constexpr int S40  = 40;
    constexpr int S48  = 48;
    constexpr int S60  = 60;

    // ── Layout Constants ──
    constexpr int MARGIN    = 16;        // Screen edge margin (was 14)
    constexpr int CARD_W    = 448;       // 480 - 2*MARGIN
    // SCREEN_W/SCREEN_H defined in config.h as macros (480x480)

    // ── Border Radius ──
    constexpr int RADIUS_SM  =  8;       // Small elements (badges, chips)
    constexpr int RADIUS_MD  = 12;       // Buttons, input fields
    constexpr int RADIUS_LG  = 16;       // Standard cards
    constexpr int RADIUS_XL  = 20;       // Hero card

    // ── Button Sizing ──
    constexpr int BTN_H_SM   = 36;
    constexpr int BTN_H_MD   = 44;
    constexpr int BTN_H_LG   = 50;
    constexpr int BTN_MIN_W  = 120;

    // ── Touch Targets ──
    constexpr int TOUCH_MIN   = 44;
    constexpr int LIST_ITEM_H = 56;

    // ── Card Properties ──
    constexpr int CARD_PAD    = 14;
    constexpr int CARD_GAP    =  6;
}

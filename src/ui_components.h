#pragma once

#include "lgfx_matouch_40.h"
#include "data_models.h"

// ── Card styles ──

// Glass card: 1px border, green-tinted fill, top highlight
void drawGlassCard(LGFX_Sprite& spr, int x, int y, int w, int h, int radius = 16);

// Hero card: like glass but with accent border and radius 20
void drawHeroCard(LGFX_Sprite& spr, int x, int y, int w, int h);

// Legacy drawCard (rounded rect with border)
void drawCard(LGFX_Sprite& spr, int x, int y, int w, int h, int radius,
              uint16_t bgColor, uint16_t borderColor);

// ── Data visualization ──

// Sparkline area chart with gradient fill
void drawSparkline(LGFX_Sprite& spr, int x, int y, int w, int h,
                   const SparklineData& data, uint16_t lineColor, uint16_t fillColor);

// Candlestick chart (OHLC bars)
void drawCandlestick(LGFX_Sprite& spr, int x, int y, int w, int h,
                     const OhlcData& data);

// Chart markers overlay: period high/low triangles + ATH dashed line
void drawChartMarkers(LGFX_Sprite& spr, int x, int y, int w, int h,
                      const SparklineData& data, float athPrice);

// Chart style badge pill (e.g. "LINE", "OHLC", "MKR")
void drawChartStyleBadge(LGFX_Sprite& spr, int x, int y, ChartStyle style);

// Horizontal dominance bar (rounded, segmented)
void drawDominanceBar(LGFX_Sprite& spr, int x, int y, int w, int h,
                      float btcPct, float ethPct,
                      uint16_t btcColor, uint16_t ethColor, uint16_t otherColor);

// Change badge — pill shape, tinted background, label + value
void drawChangeBadge(LGFX_Sprite& spr, int x, int y, int w, int h,
                     float pct, const char* label, bool selected = false);

// ── Coin components ──

// Colored circle dot for each coin
void drawCoinDot(LGFX_Sprite& spr, int cx, int cy, int r, CoinId coin);

// Mini crypto card (used in 2x2 grid)
void drawCryptoCard(LGFX_Sprite& spr, int x, int y, int w, int h,
                    const CoinData& data, CoinId coin);

// ── Layout helpers ──
void drawCentered(LGFX_Sprite& spr, const char* text, int y, const lgfx::IFont* font, uint16_t color, uint16_t bg = 0x0841);

// ── Fixed-width price rendering (eliminates bounce from proportional font) ──
// Accepts both LGFX_Sprite and tft (LGFX) via common base class
void drawFixedWidthPrice(LovyanGFX& gfx, const char* text, int cx, int cy,
                         const lgfx::IFont* font, uint16_t color);

// ── Formatters ──
void formatBtcPrice(char* buf, size_t bufSize, float price);
void formatCoinPrice(char* buf, size_t bufSize, float price, CoinId coin);
void formatArsPrice(char* buf, size_t bufSize, float price);

// ── Settings components ──
void drawSlider(LGFX_Sprite& spr, int x, int y, int w, int h, float value); // 0.0-1.0

// ── Toast ──
void showToast(const char* msg);
void updateToast();
bool isToastActive();
const char* getToastMessage();

#pragma once

#include "lgfx_matouch_40.h"
#include "colors.h"
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
                      const SparklineData& data, float athPrice, bool arsFormat = false);

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
void drawCentered(LGFX_Sprite& spr, const char* text, int y, const lgfx::IFont* font, uint16_t color, uint16_t bg = Colors::BG_CARD);

// ── Fixed-width price rendering (eliminates bounce from proportional font) ──
// Accepts both LGFX_Sprite and tft (LGFX) via common base class
void drawFixedWidthPrice(LovyanGFX& gfx, const char* text, int cx, int cy,
                         const lgfx::IFont* font, uint16_t color,
                         uint16_t bgColor = Colors::BG_BASE);

// Direct version: per-cell fill+draw for flicker-free framebuffer writes
void drawFixedWidthPriceDirect(LovyanGFX& gfx, const char* text, int cx, int cy,
                               const lgfx::IFont* font, uint16_t color,
                               uint16_t bgColor, int cellH);

// ── Formatters ──
void formatBtcPrice(char* buf, size_t bufSize, float price);
void formatCoinPrice(char* buf, size_t bufSize, float price, CoinId coin);
void formatArsPrice(char* buf, size_t bufSize, float price);
void formatPairPrice(char* buf, size_t bufSize, float price,
                     const char* prefix, const char* suffix, uint8_t decimals);

// ── Settings components ──
void drawSlider(LGFX_Sprite& spr, int x, int y, int w, int h, float value); // 0.0-1.0
void drawToggle(LGFX_Sprite& spr, int x, int y, bool on); // 44x24 toggle switch

// ── Zoomed sparkline (for pinch-to-zoom) ──
void drawSparklineZoomed(LGFX_Sprite& spr, int x, int y, int w, int h,
                         const SparklineData& data, uint16_t lineColor, uint16_t fillColor,
                         float zoomLevel, float panOffset);

// ── Polymarket prediction components ──
void drawProbabilityBar(LGFX_Sprite& spr, int x, int y, int w, int h, float yesProb);
int  drawWrappedText(LGFX_Sprite& spr, const char* text, int x, int y, int maxW,
                     int lineH, const lgfx::IFont* font, uint16_t color, int maxLines = 3,
                     bool centered = false);
void drawPredictionButton(LGFX_Sprite& spr, int x, int y, int w, int h,
                          const char* label, float probability, bool isYes, bool disabled);

// ── Toast ──
void showToast(const char* msg);
void updateToast();
bool isToastActive();
const char* getToastMessage();

#pragma once

#include <cstdint>

#define SPARKLINE_POINTS 365

// ── Coin identifiers ──
enum CoinId : uint8_t {
    COIN_BTC = 0,
    COIN_ETH,
    COIN_SOL,
    COIN_USDT,
    COIN_USDC,
    COIN_COUNT
};

// ── Per-coin market data (from CoinGecko /coins/markets) ──
struct CoinData {
    char symbol[8];       // "BTC", "ETH", etc.
    float priceUsd;
    float change1h;
    float change24h;
    float change7d;
    float marketCap;
    bool valid;
};

// ── Aggregated market data for all tracked coins ──
struct MarketData {
    CoinData coins[COIN_COUNT];
    unsigned long lastUpdate;
};

// ── Detailed BTC data (used for hero section) ──
struct BtcPrice {
    float usd;
    float change1h;
    float change24h;
    float change7d;
    float ath;
    float athChangePercent;
    bool valid;
    unsigned long lastUpdate;
};

// ── Global crypto market data ──
struct CryptoGlobal {
    float btcDominance;
    float ethDominance;
    float totalMarketCapChangePercent24h;
    float totalVolumeChangePercent24h;
    bool valid;
    unsigned long lastUpdate;
};

// ── Sparkline chart data ──
struct SparklineData {
    float points[SPARKLINE_POINTS];
    uint16_t count;
    float minVal;
    float maxVal;
    bool valid;
    unsigned long lastUpdate;
};

// ── Lemon dollar price (USDT/ARS from CriptoYa) ──
struct LemonPrice {
    float bid;        // Lemon buy price (ARS per USDT)
    float ask;        // Lemon sell price (ARS per USDT)
    bool valid;
    unsigned long lastUpdate;
};

// ── OHLC candlestick data ──
#define OHLC_MAX_BARS 365

struct OhlcBar {
    float open, high, low, close;
};

struct OhlcData {
    OhlcBar bars[OHLC_MAX_BARS];
    uint16_t count;
    float minVal, maxVal;  // global low/high across all bars
    bool valid;
    unsigned long lastUpdate;
};

// ── Chart display style ──
enum ChartStyle : uint8_t {
    CHART_LINE = 0,
    CHART_CANDLE,
    CHART_MARKERS,
    CHART_STYLE_COUNT
};

// ── Polymarket prediction data ──
#define PM_MAX_MARKETS     3
#define PM_QUESTION_LEN  120
#define PM_COND_ID_LEN    68

struct PolyMarket {
    char   question[PM_QUESTION_LEN];
    char   conditionId[PM_COND_ID_LEN];
    float  yesPrice;          // 0.0–1.0
    float  noPrice;
    float  volume24hr;
    char   endDate[24];
    bool   closed;
    bool   valid;
};

struct PolyPrediction {
    char     conditionId[PM_COND_ID_LEN];
    bool     chosenYes;
    float    probAtBet;
    uint32_t timestamp;
    uint8_t  resolved;        // 0=pending, 1=won, 2=lost
};

struct PolyStats {
    uint16_t wins, losses, pending;
    uint16_t streak, bestStreak;
};

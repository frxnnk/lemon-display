#pragma once

// ── Secrets (API keys only — WiFi credentials now come from NVS) ──
#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #define COINGECKO_API_KEY  "YOUR_COINGECKO_DEMO_KEY"
#endif

// ── App version ──
#define APP_VERSION "3.1.0"

// ── API endpoints ──
#define COINGECKO_PRICE_EP   "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true&include_1h_change=true&include_7d_change=true"
#define COINGECKO_GLOBAL_EP  "https://api.coingecko.com/api/v3/global"
#define COINGECKO_CHART_EP   "https://api.coingecko.com/api/v3/coins/bitcoin/market_chart?vs_currency=usd&days="
#define COINGECKO_CHART_EP_FMT "https://api.coingecko.com/api/v3/coins/%s/market_chart?vs_currency=usd&days="
#define COINGECKO_MARKETS_EP "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&ids=bitcoin,ethereum,solana,tether,usd-coin&order=market_cap_desc&per_page=5&page=1&sparkline=false&price_change_percentage=1h,24h,7d"
#define COINGECKO_SIMPLE_EP  "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true"
#define CRIPTOYA_LEMON_EP    "https://criptoya.com/api/lemoncash/usdt/ars"

// ── CoinGecko tether/ARS chart (for dollar sparkline) ──
#define COINGECKO_TETHER_CHART_EP "https://api.coingecko.com/api/v3/coins/tether/market_chart?vs_currency=ars&days="

// ── Binance endpoints ──
#define BINANCE_WS_HOST   "stream.binance.com"
#define BINANCE_WS_PORT   9443
#define BINANCE_WS_PATH   "/ws/btcusdt@kline_1m"
#define BINANCE_KLINES_EP "https://api.binance.com/api/v3/klines"

// ── WebSocket timing ──
#define WS_RECONNECT_MS    5000   // Reconnect every 5s on disconnect
#define WS_PING_MS        30000   // Ping interval
#define WS_PONG_TIMEOUT   10000   // Pong timeout
#define WS_DISCONNECT_CNT 3       // Missed pongs before disconnect

// ── Update intervals (ms) ──
#define UPDATE_BTC_PRICE_MS    300000   // 5min (CoinGecko % changes only, price from WS)
#define UPDATE_MARKETS_MS       60000   // 60s
#define UPDATE_GLOBAL_MS       300000   // 5min
#define UPDATE_SPARKLINE_MS    900000   // 15min
#define UPDATE_CLOCK_MS          1000   // 1s
#define UPDATE_LEMON_MS         30000   // 30s

// ── NTP config ──
#define NTP_SERVER     "pool.ntp.org"
#define GMT_OFFSET_SEC -10800  // UTC-3 (Argentina)
#define DST_OFFSET_SEC 0

// ── Display dimensions ──
#define SCREEN_W 480
#define SCREEN_H 480

// ── I2S Audio pins (MAX98357A) ──
#define I2S_DOUT  19
#define I2S_BCLK  20
#define I2S_LRCK  46

// ── Audio alert thresholds ──
#define ALERT_BTC_1H_THRESHOLD_PCT  5.0f   // BTC 1h change > 5% triggers alert

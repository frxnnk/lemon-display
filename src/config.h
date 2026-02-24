#pragma once

// ── Secrets (API keys only — WiFi credentials now come from NVS) ──
#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #define COINGECKO_API_KEY  "YOUR_COINGECKO_DEMO_KEY"
#endif

// ── App version ──
#define APP_VERSION "4.1.2"

// 0 = normal app
// 1 = minimal static display diagnostic mode
#define DISPLAY_DIAG_MODE 0

// ── API endpoints ──
#define COINGECKO_PRICE_EP   "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true&include_1h_change=true&include_7d_change=true"
#define COINGECKO_GLOBAL_EP  "https://api.coingecko.com/api/v3/global"
#define COINGECKO_CHART_EP   "https://api.coingecko.com/api/v3/coins/bitcoin/market_chart?vs_currency=usd&days="
#define COINGECKO_CHART_EP_FMT "https://api.coingecko.com/api/v3/coins/%s/market_chart?vs_currency=usd&days="
#define COINGECKO_MARKETS_EP "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&ids=bitcoin,ethereum,solana,tether,usd-coin&order=market_cap_desc&per_page=5&page=1&sparkline=false&price_change_percentage=1h,24h,7d"
#define COINGECKO_SIMPLE_EP  "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true"
#define CRIPTOYA_LEMON_EP    "https://criptoya.com/api/lemoncash/usdc/ars"

// ── CoinGecko USDC/ARS chart (for dollar sparkline) ──
#define COINGECKO_TETHER_CHART_EP "https://api.coingecko.com/api/v3/coins/usd-coin/market_chart?vs_currency=ars&days="

// ── Polymarket endpoints ──
#define POLYMARKET_GAMMA_URL "https://gamma-api.polymarket.com/markets"
#define POLYMARKET_REFRESH_MS 15000

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

// ── BTC Period definitions (8 timeframes) ──
struct PeriodDef {
    const char* label;       // Display label ("5m", "15m", "1h", etc.)
    bool        useWsBuf;    // true = derive from WS circular buffer
    const char* klineInterval; // Binance REST kline interval (null for WS-only)
    int         limit;       // WS: minutes to trim; REST: kline limit param
    bool        canOhlc;     // true = can fetch OHLC candlestick data
};

static const PeriodDef BTC_PERIODS[] = {
    // idx  label   useWs  kline   limit  ohlc
    {  "5m",  true,  nullptr,   5,  false },  // 0: 5 minutes
    { "15m",  true,  nullptr,  15,  false },  // 1: 15 minutes
    {  "1h",  true,  nullptr,  60,  false },  // 2: 1 hour
    {  "4h",  false,    "5m",  48,  true  },  // 3: 4 hours
    { "24h",  false,   "15m",  96,  true  },  // 4: 24 hours
    {  "1M",  false,    "8h",  90,  true  },  // 5: 1 month
    {  "6M",  false,    "1d", 180,  true  },  // 6: 6 months
    {  "1Y",  false,    "1d", 365,  true  },  // 7: 1 year
};
static const int BTC_PERIOD_COUNT = sizeof(BTC_PERIODS) / sizeof(BTC_PERIODS[0]);

// ── Dollar (USDT/ARS) period definitions (8 timeframes) ──
struct DollarPeriodDef {
    const char* label;
    int         days;  // CoinGecko chart days parameter
};

static const DollarPeriodDef DOLLAR_PERIODS[] = {
    {  "1d",    1 },
    {  "3d",    3 },
    {  "1w",    7 },
    {  "2w",   14 },
    {  "1M",   30 },
    {  "3M",   90 },
    {  "6M",  180 },
    {  "1Y",  365 },
};
static const int DOLLAR_PERIOD_COUNT = sizeof(DOLLAR_PERIODS) / sizeof(DOLLAR_PERIODS[0]);

// ── Audio alert thresholds ──
#define ALERT_BTC_1H_THRESHOLD_PCT  5.0f   // BTC 1h change > 5% triggers alert

// ── OTA GitHub repo ──
#define OTA_GITHUB_REPO "frxnnk/lemon-display"

// ── BTC Pair definitions (5 trading pairs) ──
enum PairSource : uint8_t {
    PAIR_BINANCE_DIRECT,   // BTC/USDT — direct WS + REST
    PAIR_BINANCE_INVERT,   // BTC/ETH, BTC/SOL — invert ETHBTC/SOLBTC
    PAIR_DERIVED,          // BTC/ARS — BTCUSDT * crossRate
    PAIR_GECKO_ONLY,       // BTC/ORO — CoinGecko only
};

struct PairDef {
    const char* label;        // Dropdown display: "USD", "ETH", "SOL", "ARS", "XAU"
    const char* pairLabel;    // Full: "BTC/USD", "BTC/ETH", etc.
    PairSource  source;
    const char* wsPath;       // Binance WS path (null for DERIVED/GECKO_ONLY)
    const char* restSymbol;   // Binance REST symbol (null for DERIVED/GECKO_ONLY)
    bool        inverted;     // true for ETHBTC/SOLBTC
    const char* geckoVs;      // CoinGecko vs_currency for chart fallback
    const char* prefix;       // "$", ""
    const char* suffix;       // "", " ETH", " SOL", " oz"
    uint8_t     decimals;     // 0=integer, 1-2=decimals
    uint8_t     minPeriodIdx; // Minimum BTC_PERIODS index (0=all, 3=4h+)
};

static const PairDef BTC_PAIRS[] = {
    //                                                                                              dec  minP
    { "USD","BTC/USD", PAIR_BINANCE_DIRECT, "/ws/btcusdt@kline_1m","BTCUSDT",false,"usd","$","",    0, 0 },
    { "ETH","BTC/ETH", PAIR_BINANCE_INVERT, "/ws/ethbtc@kline_1m", "ETHBTC", true, "eth","", " ETH",2, 0 },
    { "SOL","BTC/SOL", PAIR_BINANCE_INVERT, "/ws/solbtc@kline_1m", "SOLBTC", true, "sol","", " SOL",1, 0 },
    { "ARS","BTC/ARS", PAIR_DERIVED,        nullptr,               nullptr,  false,"ars","$","",    0, 0 },
    { "ORO","BTC/ORO", PAIR_GECKO_ONLY,     nullptr,               nullptr,  false,"xau","", " oz", 2, 3 },
};
static const int BTC_PAIR_COUNT = 5;

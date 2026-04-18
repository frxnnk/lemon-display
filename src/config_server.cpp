#include "config_server.h"
#include "data_models.h"
#include "nvs_storage.h"
#include "ui_stocks.h"
#include "config.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Arduino.h>

static AsyncWebServer* s_server = nullptr;
static char            s_ipStr[24] = "";

// Minimal dark-themed form. The captive portal already pulls in big
// PROGMEM assets; keep this one lean.
static const char WATCHLIST_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="es"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lemon &middot; Watchlist</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{background:#000;color:#fff;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;padding:24px;max-width:480px;margin:0 auto}
h1{font-size:22px;font-weight:700;margin-bottom:6px;color:#00F068}
.sub{color:#868686;font-size:14px;margin-bottom:24px}
form{display:flex;flex-direction:column;gap:16px}
label{font-size:13px;color:#868686;text-transform:uppercase;letter-spacing:1px}
textarea,input{background:#080C08;border:1px solid #2A2C2A;color:#fff;border-radius:10px;padding:14px;font-size:16px;font-family:ui-monospace,monospace;width:100%}
textarea{min-height:140px;resize:vertical}
button{background:#00F068;color:#000;border:0;border-radius:10px;padding:14px;font-size:16px;font-weight:700;cursor:pointer}
button:active{opacity:.8}
.hint{font-size:12px;color:#5B5B5B;line-height:1.4}
.flash{background:#00A849;color:#000;padding:10px 14px;border-radius:10px;margin-bottom:16px;font-weight:600}
</style></head><body>
<h1>Watchlist</h1>
<p class="sub">Up to %MAX% tickers, comma-separated. Yahoo Finance symbols (US stocks, ETFs, FX, crypto).</p>
%FLASH%
<form method="POST" action="/watchlist">
 <label>Symbols</label>
 <textarea name="symbols" placeholder="AAPL, TSLA, NVDA, SPY, MSTR">%VALUE%</textarea>
 <p class="hint">Examples: AAPL, TSLA, NVDA, SPY, QQQ, MSTR, BTC-USD, EURUSD=X</p>
 <button type="submit">Save</button>
</form>
</body></html>
)rawliteral";

static String renderWatchlistPage(const char* flash) {
    StockWatchlist wl;
    nvsLoadWatchlist(wl);
    String csv;
    for (uint8_t i = 0; i < wl.count; i++) {
        if (i > 0) csv += ", ";
        csv += wl.symbols[i];
    }
    String page(FPSTR(WATCHLIST_HTML));
    page.replace("%MAX%", String((int)STOCK_MAX_SYMBOLS));
    page.replace("%VALUE%", csv);
    if (flash && flash[0]) {
        String flashHtml = "<div class=\"flash\">";
        flashHtml += flash;
        flashHtml += "</div>";
        page.replace("%FLASH%", flashHtml);
    } else {
        page.replace("%FLASH%", "");
    }
    return page;
}

static void handleWatchlistPost(AsyncWebServerRequest* req) {
    if (!req->hasParam("symbols", true)) {
        req->send(400, "text/plain", "missing symbols field");
        return;
    }
    String raw = req->getParam("symbols", true)->value();
    // Parse into a StockWatchlist — leniently accepts commas, spaces, newlines.
    StockWatchlist wl = {};
    int start = 0;
    while (start < (int)raw.length() && wl.count < STOCK_MAX_SYMBOLS) {
        // Skip separators
        while (start < (int)raw.length()) {
            char c = raw[start];
            if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') start++;
            else break;
        }
        if (start >= (int)raw.length()) break;
        int end = start;
        while (end < (int)raw.length()) {
            char c = raw[end];
            if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
            end++;
        }
        int len = end - start;
        if (len > 0 && len < STOCK_SYMBOL_LEN) {
            char* dst = wl.symbols[wl.count];
            for (int i = 0; i < len; i++) {
                char c = raw[start + i];
                if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                dst[i] = c;
            }
            dst[len] = '\0';
            wl.count++;
        }
        start = end;
    }
    if (wl.count == 0) {
        req->send(200, "text/html", renderWatchlistPage("Watchlist cannot be empty."));
        return;
    }
    nvsSaveWatchlist(wl);
    stocksInit();                 // reload watchlist in-memory
    stocksFetchTask();            // kick off an immediate fetch so user sees fresh data
    char flash[64];
    snprintf(flash, sizeof(flash), "Saved %u symbols.", (unsigned)wl.count);
    req->send(200, "text/html", renderWatchlistPage(flash));
}

void configServerStart() {
    if (s_server) return;   // already running
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[ConfigSrv] Not starting — WiFi not connected");
        return;
    }
    s_server = new AsyncWebServer(80);

    s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->redirect("/watchlist");
    });
    s_server->on("/watchlist", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", renderWatchlistPage(nullptr));
    });
    s_server->on("/watchlist", HTTP_POST, handleWatchlistPost);
    s_server->onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "not found");
    });

    s_server->begin();

    IPAddress ip = WiFi.localIP();
    snprintf(s_ipStr, sizeof(s_ipStr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    Serial.printf("[ConfigSrv] Running on http://%s/watchlist\n", s_ipStr);
}

void configServerStop() {
    if (!s_server) return;
    s_server->end();
    delete s_server;
    s_server = nullptr;
    s_ipStr[0] = '\0';
    Serial.println("[ConfigSrv] Stopped");
}

bool configServerRunning() { return s_server != nullptr; }
const char* configServerIp() { return s_ipStr; }

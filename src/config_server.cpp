#include "config_server.h"
#include "data_models.h"
#include "nvs_storage.h"
#include "display_manager.h"
#include "colors.h"
#include "ui_dashboard.h"
#include "ui_stocks.h"
#include "scheduler.h"
#include "config.h"
#include "ota_manager.h"
#include "app_control.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_system.h>

static AsyncWebServer* s_server = nullptr;
static char            s_ipStr[24] = "";
static char            s_pairCode[8] = "";
static uint32_t        s_pairCodeUntil = 0;
static bool            s_otaArmed = false;
static bool            s_otaOk = false;
static char            s_otaError[96] = "";

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

static const char* themeName(uint8_t theme) {
    return theme == 1 ? "light" : "dark";
}

static const char* layoutName(uint8_t layout) {
    return layout == 0 ? "btc_focus" : "btc_usd";
}

static const char* z2ModeName(uint8_t mode) {
    switch (mode) {
        case Z2_MARKETS: return "markets";
        case Z2_STOCKS:  return "stocks";
        default:         return "usd";
    }
}

static bool parseWatchlistText(const String& raw, StockWatchlist& wl) {
    wl = {};
    int start = 0;
    while (start < (int)raw.length() && wl.count < STOCK_MAX_SYMBOLS) {
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
    return wl.count > 0;
}

static bool parseWatchlistJson(JsonVariantConst value, StockWatchlist& wl) {
    if (value.is<const char*>()) return parseWatchlistText(String(value.as<const char*>()), wl);
    if (!value.is<JsonArrayConst>()) return false;
    wl = {};
    for (JsonVariantConst item : value.as<JsonArrayConst>()) {
        if (wl.count >= STOCK_MAX_SYMBOLS || !item.is<const char*>()) continue;
        String symbol(item.as<const char*>());
        symbol.trim();
        symbol.toUpperCase();
        if (symbol.length() == 0 || symbol.length() >= STOCK_SYMBOL_LEN) continue;
        symbol.toCharArray(wl.symbols[wl.count], STOCK_SYMBOL_LEN);
        wl.count++;
    }
    return wl.count > 0;
}

static void scheduleStocksRefresh() {
    stocksInit();
    stocksSetActive(true);
    stocksRequestBurst();
    extern Scheduler scheduler;
    extern uint8_t   taskStocks;
    scheduler.requestRun(taskStocks);
}

static void writeWatchlistJson(JsonDocument& doc, const StockWatchlist& wl) {
    JsonArray symbols = doc["watchlist"].to<JsonArray>();
    for (uint8_t i = 0; i < wl.count; i++) symbols.add(wl.symbols[i]);
}

static bool isAllowedWebOrigin(const String& origin) {
    return origin == "https://lemon-box-landing.vercel.app" ||
           origin == "http://127.0.0.1:8765" ||
           origin == "http://localhost:8765";
}

static void macText(char* out, size_t outLen) {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(out, outLen, "%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)((mac >> 40) & 0xFF), (unsigned)((mac >> 32) & 0xFF),
             (unsigned)((mac >> 24) & 0xFF), (unsigned)((mac >> 16) & 0xFF),
             (unsigned)((mac >> 8) & 0xFF), (unsigned)(mac & 0xFF));
}

static void addCorsHeaders(AsyncWebServerRequest* req, AsyncWebServerResponse* res) {
    if (req->hasHeader("Origin")) {
        String origin = req->getHeader("Origin")->value();
        if (isAllowedWebOrigin(origin)) res->addHeader("Access-Control-Allow-Origin", origin);
    }
    char mac[24];
    macText(mac, sizeof(mac));
    res->addHeader("Vary", "Origin");
    res->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res->addHeader("Access-Control-Allow-Private-Network", "true");
    res->addHeader("Private-Network-Access-Name", "lemon-box");
    res->addHeader("Private-Network-Access-ID", mac);
}

static void sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code = 200) {
    String body;
    serializeJson(doc, body);
    AsyncWebServerResponse* res = req->beginResponse(code, "application/json", body);
    addCorsHeaders(req, res);
    req->send(res);
}

static void sendJsonError(AsyncWebServerRequest* req, int code, const char* error) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = error;
    sendJson(req, doc, code);
}

static void handleCorsOptions(AsyncWebServerRequest* req) {
    AsyncWebServerResponse* res = req->beginResponse(204, "text/plain", "");
    addCorsHeaders(req, res);
    req->send(res);
}

static void deviceId(char* out, size_t outLen) {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(out, outLen, "lemon-%06llX", (unsigned long long)(mac & 0xFFFFFFULL));
}

static bool requestAuthorized(AsyncWebServerRequest* req) {
    char token[80];
    if (!nvsGetPairingToken(token, sizeof(token))) return true;
    if (!req->hasHeader("Authorization")) return false;
    String header = req->getHeader("Authorization")->value();
    String expected = "Bearer ";
    expected += token;
    return header == expected;
}

static bool requireAuthorized(AsyncWebServerRequest* req) {
    if (requestAuthorized(req)) return true;
    sendJsonError(req, 401, "unauthorized");
    return false;
}

static void sendDeviceJson(AsyncWebServerRequest* req) {
    char id[24], mac[24];
    deviceId(id, sizeof(id));
    macText(mac, sizeof(mac));
    JsonDocument doc;
    doc["ok"] = true;
    doc["id"] = id;
    doc["name"] = "Lemon Box";
    doc["hardware"] = "esp32-s3";
    doc["version"] = APP_VERSION;
    doc["mac"] = mac;
    doc["ip"] = WiFi.localIP().toString();
    doc["brightnessControl"] = "hardware-mod-required";
    doc["brightnessControlNote"] = "MaTouch 4.0 requires R28 1k soldered and R29 removed for backlight PWM.";
    char token[8];
    doc["paired"] = nvsGetPairingToken(token, sizeof(token));
    sendJson(req, doc);
}

static void sendHealthJson(AsyncWebServerRequest* req) {
    JsonDocument doc;
    DisplayDiagnostics displayStats = displayGetDiagnostics();
    doc["ok"] = true;
    doc["status"] = "online";
    doc["version"] = APP_VERSION;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["uptimeMs"] = millis();
    doc["wifiRssi"] = WiFi.RSSI();
    JsonObject display = doc["display"].to<JsonObject>();
    display["vsyncCount"] = displayStats.vsyncCount;
    display["waitCalls"] = displayStats.waitCalls;
    display["waitTimeouts"] = displayStats.waitTimeouts;
    display["pushCount"] = displayStats.pushCount;
    display["pushedBytes"] = displayStats.pushedBytes;
    display["lastPushUs"] = displayStats.lastPushUs;
    display["maxPushUs"] = displayStats.maxPushUs;
    display["lastPushBytes"] = displayStats.lastPushBytes;
    display["maxPushBytes"] = displayStats.maxPushBytes;
    sendJson(req, doc);
}

static void handlePairStart(AsyncWebServerRequest* req) {
    uint32_t code = 100000 + (esp_random() % 900000);
    snprintf(s_pairCode, sizeof(s_pairCode), "%06lu", (unsigned long)code);
    s_pairCodeUntil = millis() + 120000UL;
    JsonDocument doc;
    doc["ok"] = true;
    doc["pairingRequired"] = true;
    doc["code"] = s_pairCode;
    doc["expiresInSec"] = 120;
    sendJson(req, doc);
}

static void handlePairConfirmBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    if (index != 0 || len != total) {
        sendJsonError(req, 413, "payload too large");
        return;
    }
    JsonDocument body;
    if (deserializeJson(body, data, len)) {
        sendJsonError(req, 400, "invalid json");
        return;
    }
    const char* code = body["code"].as<const char*>();
    if (!code || strcmp(code, s_pairCode) != 0 || millis() > s_pairCodeUntil) {
        sendJsonError(req, 403, "invalid pairing code");
        return;
    }
    char id[24];
    deviceId(id, sizeof(id));
    char token[65];
    snprintf(token, sizeof(token), "%08lX%08lX%08lX%08lX",
             (unsigned long)esp_random(), (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random());
    nvsSetPairingToken(token);
    s_pairCode[0] = '\0';
    JsonDocument doc;
    doc["ok"] = true;
    doc["id"] = id;
    doc["name"] = "Lemon Box";
    doc["version"] = APP_VERSION;
    doc["token"] = token;
    sendJson(req, doc);
}

static void handleOtaArm(AsyncWebServerRequest* req) {
    if (!requireAuthorized(req)) return;
    s_otaArmed = true;
    s_otaOk = false;
    s_otaError[0] = '\0';
    JsonDocument doc;
    doc["ok"] = true;
    doc["armed"] = true;
    doc["mode"] = "app";
    doc["recovery"] = "usb-only";
    sendJson(req, doc);
}

static void handleOtaUploadBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    bool final = (index + len) >= total;
    if (index == 0) {
        s_otaOk = false;
        s_otaError[0] = '\0';
        if (!requestAuthorized(req)) {
            strncpy(s_otaError, "unauthorized", sizeof(s_otaError) - 1);
            return;
        }
        if (!s_otaArmed) {
            strncpy(s_otaError, "ota not armed", sizeof(s_otaError) - 1);
            return;
        }
        size_t expected = total > 0 ? total : UPDATE_SIZE_UNKNOWN;
        if (!Update.begin(expected)) {
            Update.printError(Serial);
            strncpy(s_otaError, "update begin failed", sizeof(s_otaError) - 1);
            return;
        }
    }
    if (s_otaError[0]) return;
    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        strncpy(s_otaError, "update write failed", sizeof(s_otaError) - 1);
        return;
    }
    if (final) {
        s_otaOk = Update.end(true);
        if (!s_otaOk) {
            Update.printError(Serial);
            strncpy(s_otaError, "update end failed", sizeof(s_otaError) - 1);
        }
        s_otaArmed = false;
    }
}

static void sendOtaUploadResult(AsyncWebServerRequest* req) {
    if (s_otaError[0]) {
        JsonDocument doc;
        doc["ok"] = false;
        doc["error"] = s_otaError;
        sendJson(req, doc, strcmp(s_otaError, "unauthorized") == 0 ? 401 : 400);
        return;
    }
    JsonDocument doc;
    doc["ok"] = s_otaOk;
    doc["rebooting"] = s_otaOk;
    doc["mode"] = "app";
    sendJson(req, doc);
    if (s_otaOk) {
        delay(200);
        ESP.restart();
    }
}

static void handleOtaGithubBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!requireAuthorized(req)) return;
    if (index != 0 || len != total) {
        sendJsonError(req, 413, "payload too large");
        return;
    }
    JsonDocument body;
    if (len > 0 && deserializeJson(body, data, len)) {
        sendJsonError(req, 400, "invalid json");
        return;
    }
    const char* repo = body["repo"] | OTA_GITHUB_REPO;
    OtaInfo info = otaCheck(repo);
    if (!info.available) {
        JsonDocument doc;
        doc["ok"] = false;
        doc["available"] = false;
        doc["httpCode"] = info.httpCode;
        sendJson(req, doc);
        return;
    }
    bool ok = otaFlash(info.url, nullptr, info.md5[0] ? info.md5 : nullptr);
    JsonDocument doc;
    doc["ok"] = ok;
    doc["available"] = true;
    doc["version"] = info.version;
    doc["rebooting"] = ok;
    sendJson(req, doc, ok ? 200 : 500);
}

static void sendOtaStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["ok"] = true;
    doc["armed"] = s_otaArmed;
    doc["lastUploadOk"] = s_otaOk;
    doc["error"] = s_otaError;
    doc["mode"] = "app";
    doc["recovery"] = "usb-only";
    sendJson(req, doc);
}

static void sendSettingsJson(AsyncWebServerRequest* req) {
    StockWatchlist wl;
    nvsLoadWatchlist(wl);
    JsonDocument doc;
    doc["ok"] = true;
    doc["theme"] = themeName(nvsGetTheme());
    doc["layout"] = layoutName(nvsGetLayout());
    doc["brightness"] = nvsGetBrightness();
    doc["brightnessControl"] = "hardware-mod-required";
    doc["z2Mode"] = z2ModeName(nvsGetZ2Mode());
    doc["proMode"] = nvsGetProMode();
    writeWatchlistJson(doc, wl);
    sendJson(req, doc);
}

static bool parseTheme(JsonVariantConst value, uint8_t& theme) {
    if (value.is<int>()) {
        theme = value.as<int>() == 1 ? 1 : 0;
        return true;
    }
    const char* raw = value.as<const char*>();
    if (!raw) return false;
    String text(raw);
    text.toLowerCase();
    theme = text == "light" ? 1 : 0;
    return text == "light" || text == "dark";
}

static bool parseLayout(JsonVariantConst value, uint8_t& layout) {
    if (value.is<int>()) {
        layout = value.as<int>() == 0 ? 0 : 1;
        return true;
    }
    const char* raw = value.as<const char*>();
    if (!raw) return false;
    String text(raw);
    text.toLowerCase();
    if (text == "btc_focus" || text == "btc-only" || text == "btc") {
        layout = 0;
        return true;
    }
    if (text == "btc_usd" || text == "btc+usd") {
        layout = 1;
        return true;
    }
    return false;
}

static bool parseZ2Mode(JsonVariantConst value, uint8_t& mode) {
    if (value.is<int>()) {
        mode = constrain(value.as<int>(), 0, (int)Z2_COUNT - 1);
        return true;
    }
    const char* raw = value.as<const char*>();
    if (!raw) return false;
    String text(raw);
    text.toLowerCase();
    if (text == "markets") mode = Z2_MARKETS;
    else if (text == "stocks") mode = Z2_STOCKS;
    else if (text == "usd" || text == "dollar") mode = Z2_USD;
    else return false;
    return true;
}

static void handleSettingsBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!requireAuthorized(req)) return;
    if (index != 0 || len != total) {
        sendJsonError(req, 413, "payload too large");
        return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        sendJsonError(req, 400, "invalid json");
        return;
    }
    if (!doc["theme"].isNull()) {
        uint8_t theme;
        if (parseTheme(doc["theme"], theme)) {
            nvsSetTheme(theme);
            Colors::setTheme(theme == 1 ? Colors::THEME_LIGHT : Colors::THEME_DARK);
        }
    }
    if (!doc["layout"].isNull()) {
        uint8_t layout;
        if (parseLayout(doc["layout"], layout)) {
            nvsSetLayout(layout);
            dashboardSetLayout(layout);
        }
    }
    if (doc["brightness"].is<int>()) {
        uint8_t brightness = constrain(doc["brightness"].as<int>(), 1, 255);
        nvsSetBrightness(brightness);
        displaySetBrightness(brightness);
    }
    if (doc["proMode"].is<bool>()) {
        nvsSetProMode(doc["proMode"].as<bool>());
    }
    if (!doc["z2Mode"].isNull()) {
        uint8_t mode;
        if (parseZ2Mode(doc["z2Mode"], mode)) {
            appApplyZ2Mode((Z2Mode)mode);
        }
    }
    StockWatchlist wl;
    if (parseWatchlistJson(doc["watchlist"], wl)) {
        nvsSaveWatchlist(wl);
        scheduleStocksRefresh();
    }
    dashboardMarkAllDirty();
    sendSettingsJson(req);
}

static void sendWatchlistJson(AsyncWebServerRequest* req) {
    StockWatchlist wl;
    nvsLoadWatchlist(wl);
    JsonDocument doc;
    doc["ok"] = true;
    writeWatchlistJson(doc, wl);
    sendJson(req, doc);
}

static void handleWatchlistJsonBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!requireAuthorized(req)) return;
    if (index != 0 || len != total) {
        sendJsonError(req, 413, "payload too large");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        sendJsonError(req, 400, "invalid json");
        return;
    }
    StockWatchlist wl;
    JsonVariantConst value = !doc["watchlist"].isNull() ? doc["watchlist"] : doc["symbols"];
    if (!parseWatchlistJson(value, wl)) {
        sendJsonError(req, 400, "watchlist cannot be empty");
        return;
    }
    nvsSaveWatchlist(wl);
    scheduleStocksRefresh();
    sendWatchlistJson(req);
}

static void handleWatchlistPost(AsyncWebServerRequest* req) {
    if (!req->hasParam("symbols", true)) {
        req->send(400, "text/plain", "missing symbols field");
        return;
    }
    String raw = req->getParam("symbols", true)->value();
    StockWatchlist parsed;
    if (!parseWatchlistText(raw, parsed)) {
        req->send(200, "text/html", renderWatchlistPage("Watchlist cannot be empty."));
        return;
    }
    nvsSaveWatchlist(parsed);
    scheduleStocksRefresh();
    char parsedFlash[64];
    snprintf(parsedFlash, sizeof(parsedFlash), "Saved %u symbols.", (unsigned)parsed.count);
    req->send(200, "text/html", renderWatchlistPage(parsedFlash));
    return;
#if 0
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
    // Defer the actual fetch to the scheduler so we don't block the HTTP
    // response for 3-8 s while Yahoo responds.
    {
        extern Scheduler scheduler;
        extern uint8_t   taskStocks;
        scheduler.requestRun(taskStocks);
    }
    char flash[64];
    snprintf(flash, sizeof(flash), "Saved %u symbols.", (unsigned)wl.count);
    req->send(200, "text/html", renderWatchlistPage(flash));
#endif
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
    s_server->on("/api/device", HTTP_OPTIONS, handleCorsOptions);
    s_server->on("/api/health", HTTP_OPTIONS, handleCorsOptions);
    s_server->on("/api/pair/start", HTTP_OPTIONS, handleCorsOptions);
    s_server->on("/api/pair/confirm", HTTP_OPTIONS, handleCorsOptions);
    s_server->on("/api/settings", HTTP_OPTIONS, handleCorsOptions);
    s_server->on("/api/watchlist", HTTP_OPTIONS, handleCorsOptions);
    s_server->on("/api/ota/github", HTTP_OPTIONS, handleCorsOptions);
    s_server->on("/api/ota/status", HTTP_OPTIONS, handleCorsOptions);
    s_server->on("/api/device", HTTP_GET, sendDeviceJson);
    s_server->on("/api/health", HTTP_GET, sendHealthJson);
    s_server->on("/api/pair/start", HTTP_POST, handlePairStart);
    s_server->on("/api/pair/confirm", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr, handlePairConfirmBody);
    s_server->on("/api/ota/arm", HTTP_POST, handleOtaArm);
    s_server->on("/api/ota/upload", HTTP_POST, sendOtaUploadResult, nullptr, handleOtaUploadBody);
    s_server->on("/api/ota/github", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr, handleOtaGithubBody);
    s_server->on("/api/ota/status", HTTP_GET, sendOtaStatus);
    s_server->on("/api/settings", HTTP_GET, sendSettingsJson);
    s_server->on("/api/settings", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr, handleSettingsBody);
    s_server->on("/api/watchlist", HTTP_GET, sendWatchlistJson);
    s_server->on("/api/watchlist", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr, handleWatchlistJsonBody);
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

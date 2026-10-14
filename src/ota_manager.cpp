#include "ota_manager.h"
#include "config.h"
#include "display_manager.h"
#include "colors.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

extern const char* ROOT_CAS;  // Defined in api_client.cpp

// On-screen debug for OTA (no serial needed)
static int otaDbgY = 40;
static int otaProgressY = -1;  // Fixed Y for progress line (in-place update)
static void otaScreen(const char* msg, uint16_t color = 0xFFFF, bool inPlace = false) {
    int y = inPlace && otaProgressY >= 0 ? otaProgressY : otaDbgY;
    if (inPlace && otaProgressY < 0) otaProgressY = otaDbgY;  // Lock Y on first call
    if (inPlace) tft.fillRect(10, y, SCREEN_W - 20, 18, 0x0000);  // Clear previous text
    tft.setTextColor(color, 0x0000);
    tft.setTextDatum(lgfx::top_left);
    tft.drawString(msg, 10, y, &lgfx::fonts::Font2);
    if (!inPlace) otaDbgY += 18;
    Serial.println(msg);
}

// Parse "M.m.p" or "M.m.p-beta.N" / "M.m.p-rcN" etc.
// Stable release (no prerelease suffix) is treated as INT_MAX so it always
// wins against any prerelease of the same M.m.p.
static void parseVersion(const char* s, int& maj, int& min, int& pat, int& pre) {
    maj = min = pat = 0;
    pre = 0x7FFFFFFF;  // stable = highest
    if (!s) return;
    sscanf(s, "%d.%d.%d", &maj, &min, &pat);
    const char* dash = strchr(s, '-');
    if (!dash) return;
    const char* p = dash + 1;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return;
    int n = 0;
    sscanf(p, "%d", &n);
    pre = n;
}

// Semver-ish comparison: returns true if remote > local. Recognises a
// numeric prerelease suffix (beta.N, rcN) so 5.1.0-beta.7 < 5.1.0-beta.8 <
// 5.1.0 works correctly.
static bool isNewer(const char* remote, const char* local) {
    int rMaj, rMin, rPat, rPre;
    int lMaj, lMin, lPat, lPre;
    parseVersion(remote, rMaj, rMin, rPat, rPre);
    parseVersion(local,  lMaj, lMin, lPat, lPre);
    if (rMaj != lMaj) return rMaj > lMaj;
    if (rMin != lMin) return rMin > lMin;
    if (rPat != lPat) return rPat > lPat;
    return rPre > lPre;
}

// File-scope so otaFreeCheck() can release TLS buffers
static WiFiClientSecure checkClient;
static HTTPClient checkHttp;
static WiFiClientSecure probeClient;
static HTTPClient probeHttp;

void otaFreeCheck() {
    checkHttp.end();
    checkClient.stop();
    Serial.printf("[OTA] Freed check TLS, heap: %d\n", (int)ESP.getFreeHeap());
}

static bool isHexChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static void normalizeMd5Lower(char value[33]) {
    for (char* c = value; *c; ++c) {
        if (*c >= 'A' && *c <= 'F') *c = static_cast<char>(*c - 'A' + 'a');
    }
}

static bool extractMd5NearAsset(const char* body, const char* assetName, char out[33]) {
    out[0] = '\0';
    if (!body || !assetName) return false;
    const char* start = strstr(body, assetName);
    if (!start && strcmp(assetName, "firmware.bin") == 0) start = body;
    if (!start) return false;
    const char* end = start + strlen(start);
    if (end > start + 192) end = start + 192;
    for (const char* p = start; p + 32 <= end; ++p) {
        bool valid = true;
        for (int i = 0; i < 32; ++i) {
            if (!isHexChar(p[i])) {
                valid = false;
                break;
            }
        }
        if (valid && (p == start || !isHexChar(p[-1])) && !isHexChar(p[32])) {
            memcpy(out, p, 32);
            out[32] = '\0';
            normalizeMd5Lower(out);
            return true;
        }
    }
    return false;
}

bool otaLatestTagChanged(const char* repo, const char* localVersion) {
    if (!repo || !localVersion || WiFi.status() != WL_CONNECTED) return false;
    static char url[192];
    snprintf(url, sizeof(url), "https://github.com/%s/releases/latest", repo);
    probeClient.setInsecure();
    if (!probeHttp.begin(probeClient, url)) return false;
    probeHttp.setConnectTimeout(5000);
    probeHttp.setTimeout(5000);
    probeHttp.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    int code = probeHttp.sendRequest("HEAD");
    String location = probeHttp.getLocation();
    probeHttp.end();
    probeClient.stop();
    if ((code != 301 && code != 302 && code != 303 && code != 307 && code != 308) ||
        location.isEmpty()) return false;
    int tagAt = location.lastIndexOf("/tag/");
    if (tagAt < 0) return false;
    String remote = location.substring(tagAt + 5);
    if (remote.startsWith("v") || remote.startsWith("V")) remote.remove(0, 1);
    bool changed = isNewer(remote.c_str(), localVersion);
    if (changed) Serial.printf("[OTA] Latest tag changed: %s -> %s\n", localVersion, remote.c_str());
    return changed;
}

OtaInfo otaCheckAsset(const char* repo, const char* assetName, const char* localVersion) {
    OtaInfo info = {};
    info.available = false;

    // TLS handshake needs ~20KB contiguous. When the device has been running
    // for a while, api_client's secureClient + the WS TLS session leave only
    // ~50KB free with bad fragmentation, and mbedtls fails → HTTP -1. Drop
    // the shared TLS session first (scheduler re-opens on its next fetch)
    // and ask mbedtls for small per-connection buffers so this one fits.
    extern void apiStop();
    apiStop();

    checkClient.setInsecure();

    static char url[256];    // static: off the stack
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", repo);

    checkHttp.begin(checkClient, url);
    checkHttp.addHeader("Accept", "application/vnd.github.v3+json");
    checkHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    checkHttp.setTimeout(10000);

    int code = checkHttp.GET();
    info.httpCode = code;
    if (code != 200) {
        Serial.printf("[OTA] GitHub API error: %d\n", code);
        checkHttp.end();
        checkClient.stop();
        return info;
    }

    String body = checkHttp.getString();
    checkHttp.end();
    checkClient.stop();

    // Parse only release metadata and asset names/URLs. The filter prototype at
    // index 0 applies to every element in the JSON array.
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    filter["body"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(10));

    if (err) {
        Serial.printf("[OTA] JSON parse error: %s\n", err.c_str());
        return info;
    }

    const char* tag = doc["tag_name"] | (const char*)nullptr;
    if (!tag) return info;

    const char* ver = tag;
    if (ver[0] == 'v' || ver[0] == 'V') ver++;
    strncpy(info.version, ver, sizeof(info.version) - 1);

    if (!isNewer(info.version, localVersion)) {
        Serial.printf("[OTA] Up to date: %s (remote: %s)\n", localVersion, info.version);
        return info;
    }

    const char* assetUrl = nullptr;
    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        const char* name = asset["name"] | (const char*)nullptr;
        if (name && strcmp(name, assetName) == 0) {
            assetUrl = asset["browser_download_url"] | (const char*)nullptr;
            break;
        }
    }
    if (!assetUrl) {
        Serial.printf("[OTA] Exact asset not found: %s\n", assetName);
        return info;
    }
    Serial.printf("[OTA] Asset browser URL: %s\n", assetUrl);

    strncpy(info.url, assetUrl, sizeof(info.url) - 1);
    info.available = true;

    // Channel-specific checksum format: "firmware-v2.bin MD5: <hash>".
    // The legacy firmware.bin channel still accepts its historical standalone hash.
    info.md5[0] = '\0';
    const char* bodyStr = doc["body"] | (const char*)nullptr;
    if (extractMd5NearAsset(bodyStr, assetName, info.md5)) {
        Serial.printf("[OTA] MD5 for %s: %s\n", assetName, info.md5);
    }

    Serial.printf("[OTA] Update available: %s -> %s\n", localVersion, info.version);
    Serial.printf("[OTA] URL: %s\n", info.url);
    return info;
}

OtaInfo otaCheck(const char* repo) {
    return otaCheckAsset(repo, "firmware.bin", APP_VERSION);
}

bool otaFlash(const char* binUrl, void(*progressCB)(int pct), const char* md5) {
    // Release any TLS buffers still bound by the last otaCheck() — a fresh
    // handshake needs ~40KB of DRAM, and a lingering session would starve
    // it and yield HTTP -1 on the redirect request.
    otaFreeCheck();

    // Show debug on screen
    tft.fillScreen(0x0000);
    displaySetBrightness(128);
    otaDbgY = 10;
    otaProgressY = -1;
    otaScreen("OTA Flash starting...");

    static char dbg[128];
    snprintf(dbg, sizeof(dbg), "Heap: %d", (int)ESP.getFreeHeap());
    otaScreen(dbg);

    Serial.printf("[OTA] Starting flash from: %s\n", binUrl);

    // ── Step 1: Resolve redirect (GitHub always 302s to CDN) ──
    otaScreen("Step 1: GitHub API redirect...");
    static WiFiClientSecure client;
    static HTTPClient http;

    // Retry on HTTPC_ERROR_CONNECTION_FAILED (-1) — on devices with fragmented
    // DRAM the first TLS handshake can fail even when total free heap says
    // there's plenty available. Give it a few shots with a delay in between.
    int code = -1;
    for (int attempt = 0; attempt < 3 && code < 0; attempt++) {
        if (attempt > 0) {
            snprintf(dbg, sizeof(dbg), "  Retry %d (h=%u m=%u)",
                     attempt,
                     (unsigned)(ESP.getFreeHeap() / 1024),
                     (unsigned)(ESP.getMaxAllocHeap() / 1024));
            otaScreen(dbg, 0xFBE0);
            http.end();
            client.stop();
            esp_task_wdt_reset();
            delay(500);
            esp_task_wdt_reset();
        }

        client.setInsecure();
        http.begin(client, binUrl);
        if (attempt == 0) otaScreen("  Auth: none", 0xFBE0);
        http.addHeader("Accept", "application/octet-stream");
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        http.setTimeout(30000);

        esp_task_wdt_reset();
        code = http.GET();
        esp_task_wdt_reset();
    }

    snprintf(dbg, sizeof(dbg), "  Response: %d", code);
    otaScreen(dbg, (code == 302 || code == 301) ? 0x07E0 : 0xF800);

    String finalUrl;
    if (code == 301 || code == 302) {
        finalUrl = http.getLocation();
        otaScreen("  Got redirect OK", 0x07E0);
    } else if (code == 200) {
        finalUrl = "";
        otaScreen("  Direct download (no redirect)");
    } else {
        snprintf(dbg, sizeof(dbg), "  FAIL step1: HTTP %d", code);
        otaScreen(dbg, 0xF800);
        http.end();
        esp_task_wdt_reset();
        delay(2000);
        return false;
    }
    http.end();
    client.stop();
    esp_task_wdt_reset();

    // ── Step 2: Download firmware from CDN (fresh TLS connection) ──
    otaScreen("Step 2: Download from CDN...");
    if (finalUrl.length() > 0) {
        client.setCACert(ROOT_CAS);
        http.begin(client, finalUrl);
        http.setTimeout(60000);
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

        code = http.GET();
        snprintf(dbg, sizeof(dbg), "  CDN response: %d", code);
        otaScreen(dbg, (code == 200) ? 0x07E0 : 0xF800);
    }

    if (code != 200) {
        snprintf(dbg, sizeof(dbg), "  FAIL download: HTTP %d", code);
        otaScreen(dbg, 0xF800);
        http.end();
        esp_task_wdt_reset();
        delay(2000);
        return false;
    }

    int contentLen = http.getSize();
    snprintf(dbg, sizeof(dbg), "  Size: %d bytes", contentLen);
    otaScreen(dbg);

    if (contentLen <= 0) {
        otaScreen("  FAIL: invalid content length", 0xF800);
        http.end();
        esp_task_wdt_reset();
        delay(2000);
        return false;
    }

    // ── Step 3: Flash ──
    otaScreen("Step 3: Flashing...");

    if (!Update.begin(contentLen)) {
        snprintf(dbg, sizeof(dbg), "  FAIL begin: %s", Update.errorString());
        otaScreen(dbg, 0xF800);
        http.end();
        esp_task_wdt_reset();
        delay(2000);
        return false;
    }

    if (md5 && md5[0] != '\0') {
        Update.setMD5(md5);
        snprintf(dbg, sizeof(dbg), "  MD5: %s", md5);
        otaScreen(dbg);
    }

    WiFiClient* stream = http.getStreamPtr();
    static uint8_t buf[4096];
    size_t written = 0;
    int lastPct = -1;

    while (written < (size_t)contentLen) {
        size_t available = stream->available();
        if (available == 0) {
            unsigned long waitStart = millis();
            while (stream->available() == 0 && millis() - waitStart < 10000) {
                delay(10);
                esp_task_wdt_reset();
            }
            if (stream->available() == 0) {
                otaScreen("  FAIL: stream timeout", 0xF800);
                Update.abort();
                http.end();
                delay(5000);
                return false;
            }
            continue;
        }

        size_t toRead = (available < sizeof(buf)) ? available : sizeof(buf);
        int bytesRead = stream->readBytes(buf, toRead);
        if (bytesRead <= 0) break;

        size_t w = Update.write(buf, bytesRead);
        if (w != (size_t)bytesRead) {
            snprintf(dbg, sizeof(dbg), "  FAIL write: %d vs %d", (int)w, bytesRead);
            otaScreen(dbg, 0xF800);
            Update.abort();
            http.end();
            delay(5000);
            return false;
        }

        written += bytesRead;
        int pct = (int)((written * 100) / contentLen);
        if (pct != lastPct) {
            lastPct = pct;
            esp_task_wdt_reset();  // Reset WDT every percent to prevent timeout on slow networks
            if (pct % 5 == 0) {
                snprintf(dbg, sizeof(dbg), "  Progress: %d%%", pct);
                otaScreen(dbg, 0x07E0, true);  // in-place update
            }
            if (progressCB) progressCB(pct);
        }
    }

    snprintf(dbg, sizeof(dbg), "Written: %d / %d", (int)written, contentLen);
    otaScreen(dbg);

    if (!Update.end()) {
        snprintf(dbg, sizeof(dbg), "FAIL end: %s", Update.errorString());
        otaScreen(dbg, 0xF800);
        http.end();
        esp_task_wdt_reset();
        delay(2000);
        return false;
    }

    http.end();

    if (Update.isFinished()) {
        otaScreen("SUCCESS! Rebooting...", 0x07E0);
        delay(2000);
        ESP.restart();
        return true;
    }

    otaScreen("FAIL: not finished", 0xF800);
    delay(5000);
    return false;
}

#include "ota_manager.h"
#include "config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>

// Simple semver comparison: returns true if remote > local
static bool isNewer(const char* remote, const char* local) {
    int rMaj = 0, rMin = 0, rPat = 0;
    int lMaj = 0, lMin = 0, lPat = 0;
    sscanf(remote, "%d.%d.%d", &rMaj, &rMin, &rPat);
    sscanf(local, "%d.%d.%d", &lMaj, &lMin, &lPat);
    if (rMaj != lMaj) return rMaj > lMaj;
    if (rMin != lMin) return rMin > lMin;
    return rPat > lPat;
}

OtaInfo otaCheck(const char* repo) {
    OtaInfo info = {};
    info.available = false;

    HTTPClient http;
    char url[256];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", repo);

    http.begin(url);
    http.addHeader("Accept", "application/vnd.github.v3+json");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[OTA] GitHub API error: %d\n", code);
        http.end();
        return info;
    }

    String body = http.getString();
    http.end();

    // Parse tag_name (e.g. "v4.1.0" or "4.1.0")
    int tagIdx = body.indexOf("\"tag_name\"");
    if (tagIdx < 0) return info;
    int tagStart = body.indexOf('"', tagIdx + 10) + 1;
    int tagEnd = body.indexOf('"', tagStart);
    if (tagStart <= 0 || tagEnd <= tagStart) return info;

    String tag = body.substring(tagStart, tagEnd);
    // Strip leading 'v' if present
    const char* ver = tag.c_str();
    if (ver[0] == 'v' || ver[0] == 'V') ver++;
    strncpy(info.version, ver, sizeof(info.version) - 1);

    if (!isNewer(info.version, APP_VERSION)) {
        Serial.printf("[OTA] Up to date: %s (remote: %s)\n", APP_VERSION, info.version);
        return info;
    }

    // Find .bin asset URL in browser_download_url
    int binIdx = body.indexOf(".bin\"");
    if (binIdx < 0) {
        Serial.println("[OTA] No .bin asset found in release");
        return info;
    }

    // Search backwards for "browser_download_url":"
    int urlKey = body.lastIndexOf("\"browser_download_url\"", binIdx);
    if (urlKey < 0) return info;
    int urlStart = body.indexOf('"', urlKey + 22) + 1;
    int urlEnd = body.indexOf('"', urlStart);
    if (urlStart <= 0 || urlEnd <= urlStart) return info;

    String binUrl = body.substring(urlStart, urlEnd);
    strncpy(info.url, binUrl.c_str(), sizeof(info.url) - 1);
    info.available = true;

    Serial.printf("[OTA] Update available: %s -> %s\n", APP_VERSION, info.version);
    Serial.printf("[OTA] URL: %s\n", info.url);
    return info;
}

bool otaFlash(const char* binUrl) {
    HTTPClient http;
    http.begin(binUrl);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[OTA] Download failed: %d\n", code);
        http.end();
        return false;
    }

    int contentLen = http.getSize();
    if (contentLen <= 0) {
        Serial.println("[OTA] Invalid content length");
        http.end();
        return false;
    }

    Serial.printf("[OTA] Downloading %d bytes...\n", contentLen);

    if (!Update.begin(contentLen)) {
        Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    Serial.printf("[OTA] Written: %d / %d\n", (int)written, contentLen);

    if (!Update.end()) {
        Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString());
        http.end();
        return false;
    }

    http.end();

    if (Update.isFinished()) {
        Serial.println("[OTA] Success! Rebooting...");
        delay(500);
        ESP.restart();
        return true;  // Won't reach here
    }

    Serial.println("[OTA] Update not finished");
    return false;
}

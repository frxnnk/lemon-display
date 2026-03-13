#include "ota_manager.h"
#include "config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

extern const char* ROOT_CAS;  // Defined in api_client.cpp

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

    static WiFiClientSecure client;
    client.setCACert(ROOT_CAS);

    HTTPClient http;
    char url[256];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", repo);

    http.begin(client, url);
    http.addHeader("Accept", "application/vnd.github.v3+json");
#ifdef GITHUB_PAT
    http.addHeader("Authorization", "Bearer " GITHUB_PAT);
#endif
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000);

    int code = http.GET();
    info.httpCode = code;
    if (code != 200) {
        Serial.printf("[OTA] GitHub API error: %d\n", code);
        http.end();
        return info;
    }

    String body = http.getString();
    http.end();

    // Parse with ArduinoJson (filter: only tag_name + first asset download URL)
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["assets"][0]["browser_download_url"] = true;

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

    if (!isNewer(info.version, APP_VERSION)) {
        Serial.printf("[OTA] Up to date: %s (remote: %s)\n", APP_VERSION, info.version);
        return info;
    }

    const char* assetUrl = doc["assets"][0]["browser_download_url"] | (const char*)nullptr;
    if (!assetUrl) {
        Serial.println("[OTA] No asset found in release");
        return info;
    }

    strncpy(info.url, assetUrl, sizeof(info.url) - 1);
    info.available = true;

    Serial.printf("[OTA] Update available: %s -> %s\n", APP_VERSION, info.version);
    Serial.printf("[OTA] URL: %s\n", info.url);
    return info;
}

bool otaFlash(const char* binUrl, void(*progressCB)(int pct)) {
    static WiFiClientSecure client;
    client.setInsecure();  // GitHub CDN uses different CAs — skip verification

    HTTPClient http;
    http.begin(client, binUrl);
#ifdef GITHUB_PAT
    http.addHeader("Authorization", "Bearer " GITHUB_PAT);
#endif
    http.addHeader("Accept", "application/octet-stream");
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(60000);

    Serial.printf("[OTA] Requesting: %s\n", binUrl);
    Serial.printf("[OTA] Free heap: %d\n", ESP.getFreeHeap());

    esp_task_wdt_reset();
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[OTA] Download failed: HTTP %d\n", code);
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
    static uint8_t buf[4096];
    size_t written = 0;
    int lastPct = -1;

    while (written < (size_t)contentLen) {
        size_t available = stream->available();
        if (available == 0) {
            // Wait for data with timeout
            unsigned long waitStart = millis();
            while (stream->available() == 0 && millis() - waitStart < 10000) {
                delay(10);
            }
            if (stream->available() == 0) {
                Serial.println("[OTA] Stream timeout");
                Update.abort();
                http.end();
                return false;
            }
            continue;
        }

        size_t toRead = (available < sizeof(buf)) ? available : sizeof(buf);
        int bytesRead = stream->readBytes(buf, toRead);
        if (bytesRead <= 0) break;

        size_t w = Update.write(buf, bytesRead);
        if (w != (size_t)bytesRead) {
            Serial.printf("[OTA] Write mismatch: %d vs %d\n", (int)w, bytesRead);
            Update.abort();
            http.end();
            return false;
        }

        written += bytesRead;
        int pct = (int)((written * 100) / contentLen);
        if (pct != lastPct) {
            lastPct = pct;
            Serial.printf("[OTA] Progress: %d%%\n", pct);
            if (progressCB) progressCB(pct);
            esp_task_wdt_reset();
        }
    }

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

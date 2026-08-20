#include "api_client.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <cctype>
#include <cmath>
#include <cstring>
#include <esp_task_wdt.h>

// Root CAs for every HTTPS provider used by the firmware.
const char* ROOT_CAS =
    // GTS Root R4 — CoinGecko, CriptoYa, Polymarket
    "-----BEGIN CERTIFICATE-----\n"
    "MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD\n"
    "VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\n"
    "A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\n"
    "WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\n"
    "IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\n"
    "AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi\n"
    "QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR\n"
    "HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\n"
    "BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D\n"
    "9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8\n"
    "p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD\n"
    "-----END CERTIFICATE-----\n"
    // GTS Root R1 — Vercel network metrics endpoint
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c\n"
    "-----END CERTIFICATE-----\n"
    // USERTrust ECC — GitHub (OTA)
    "-----BEGIN CERTIFICATE-----\n"
    "MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL\n"
    "MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl\n"
    "eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT\n"
    "JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx\n"
    "MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT\n"
    "Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg\n"
    "VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm\n"
    "aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo\n"
    "I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng\n"
    "o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G\n"
    "A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD\n"
    "VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB\n"
    "zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW\n"
    "RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=\n"
    "-----END CERTIFICATE-----\n"
    // DigiCert Global Root G2 — Binance
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaT\n"
    "epLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTf\n"
    "lMrY=\n"
    "-----END CERTIFICATE-----\n"
    // ISRG Root X1 — Let's Encrypt (GitHub release-assets CDN)
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n"
    // Amazon Root CA 1 — api.lemoncash.com.ar
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
    "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
    "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
    "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
    "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
    "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
    "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
    "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
    "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
    "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
    "rqXRfboQnoZsG4q5WTP468SQvvG5\n"
    "-----END CERTIFICATE-----\n";

static WiFiClientSecure secureClient;
static unsigned long lastCoinGeckoCall = 0;
static const unsigned long COINGECKO_MIN_INTERVAL = 6000;  // 6s between CoinGecko calls

static bool coinGeckoKeyConfigured() {
    return COINGECKO_API_KEY[0] != '\0' &&
           strcmp(COINGECKO_API_KEY, "YOUR_COINGECKO_DEMO_KEY") != 0;
}

void apiSetup() {
    secureClient.setCACert(ROOT_CAS);
    secureClient.setHandshakeTimeout(5);   // 5s max for TLS handshake
    secureClient.setTimeout(7);            // 7s general socket timeout
}

void apiStop() {
    secureClient.stop();
}

// ── PSRAM response buffer: allocated once, reused — eliminates heap fragmentation ──
static char* _rspBuf = nullptr;
static const size_t RSP_BUF_SIZE = 98304;  // 96KB max API response (in PSRAM) — fits BTC 1Y klines (~67KB)

// Stream adapter that writes into _rspBuf. Used with HTTPClient::writeToStream()
// so chunked Transfer-Encoding is parsed correctly (getStreamPtr() would leave
// chunk size markers in the data and break JSON parsing).
class PSRAMBufStream : public Stream {
public:
    int bytesWritten = 0;
    size_t write(uint8_t c) override {
        if (bytesWritten >= (int)(RSP_BUF_SIZE - 1)) return 0;
        _rspBuf[bytesWritten++] = (char)c;
        return 1;
    }
    size_t write(const uint8_t* buf, size_t size) override {
        int avail = (int)(RSP_BUF_SIZE - 1) - bytesWritten;
        int toWrite = (int)size;
        if (toWrite > avail) toWrite = avail;
        if (toWrite > 0) {
            memcpy(_rspBuf + bytesWritten, buf, toWrite);
            bytesWritten += toWrite;
            esp_task_wdt_reset();
        }
        return toWrite;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};

// ── Helper: perform HTTPS GET with 1 retry, response in PSRAM buffer ──
// Exposed as apiHttpGet() via api_client.h so sibling clients (stocks, poly)
// can reuse the hardened TLS / chunked / WDT logic.
const char* apiHttpGet(const char* url, bool addCoinGeckoKey, ApiResult& result,
                       int timeoutMs, uint32_t maxBodyBytes, int maxAttempts) {
    // Allocate PSRAM buffer once (persists for device lifetime)
    if (!_rspBuf) {
        _rspBuf = (char*)ps_malloc(RSP_BUF_SIZE);
        if (!_rspBuf) _rspBuf = (char*)malloc(RSP_BUF_SIZE);  // fallback
        if (!_rspBuf) {
            Serial.println("[API] FATAL: cannot allocate response buffer");
            result = API_NETWORK_ERROR;
            static char empty[1] = {0};
            return empty;
        }
    }
    _rspBuf[0] = '\0';

    const bool appendCoinGeckoKey =
        addCoinGeckoKey && coinGeckoKeyConfigured();

    // Rate limit CoinGecko calls to avoid 429s — wait if too soon
    if (addCoinGeckoKey && lastCoinGeckoCall > 0) {
        unsigned long now = millis();
        unsigned long elapsed = now - lastCoinGeckoCall;
        if (elapsed < COINGECKO_MIN_INTERVAL) {
            unsigned long wait = COINGECKO_MIN_INTERVAL - elapsed;
            Serial.printf("[API] CoinGecko rate limit: waiting %lums\n", wait);
            unsigned long waitEnd = millis() + wait;
            while (millis() < waitEnd) {
                delay(100);
                esp_task_wdt_reset();
            }
        }
    }
    if (addCoinGeckoKey) lastCoinGeckoCall = millis();

    if (maxAttempts < 1) maxAttempts = 1;
    for (int attempt = 0; attempt < maxAttempts; attempt++) {
        esp_task_wdt_reset();
        if (attempt > 0) {
            Serial.printf("[API] Retry %d for %s\n", attempt, url);
            delay(1000);
        }

        static HTTPClient http;   // static: ~700 bytes off the 8KB stack
        http.setConnectTimeout(timeoutMs < 5000 ? timeoutMs : 5000);
        http.setTimeout(timeoutMs);
        // Prefix reads use HTTP/1.0 so the body arrives as a close-delimited
        // stream instead of chunk framing. This lets callers stop cleanly
        // after the useful prefix without downloading the full document.
        http.useHTTP10(maxBodyBytes > 0);

        static char fullUrl[512]; // static: 512 bytes off the stack
        if (appendCoinGeckoKey) {
            const char* sep = (strchr(url, '?') != nullptr) ? "&" : "?";
            snprintf(fullUrl, sizeof(fullUrl), "%s%sx_cg_demo_api_key=%s", url, sep, COINGECKO_API_KEY);
        } else {
            strncpy(fullUrl, url, sizeof(fullUrl) - 1);
            fullUrl[sizeof(fullUrl) - 1] = '\0';
        }

        if (!http.begin(secureClient, fullUrl)) {
            Serial.printf("[API] Failed to begin: %s\n", url);
            result = API_NETWORK_ERROR;
            http.end();
            continue;
        }
        http.addHeader("Accept", "application/json");
        // Some APIs (notably Yahoo Finance) return 401/429 for the default
        // "ESP32HTTPClient" user-agent. A neutral browser-ish UA avoids that
        // without affecting the other CoinGecko / Binance / Criptoya endpoints.
        http.setUserAgent("Mozilla/5.0 (compatible; Lemon-Box/5.0)");

        esp_task_wdt_reset();
        int code = http.GET();
        esp_task_wdt_reset();
        if (code == 200) {
            int contentLen = http.getSize();  // -1 if chunked / unknown
            int bytesRead = 0;
            const int bodyLimit =
                maxBodyBytes > 0 && maxBodyBytes < (RSP_BUF_SIZE - 1)
                    ? static_cast<int>(maxBodyBytes)
                    : static_cast<int>(RSP_BUF_SIZE - 1);

            if (maxBodyBytes > 0) {
                WiFiClient* stream = http.getStreamPtr();
                const int targetBytes =
                    contentLen > 0 && contentLen < bodyLimit ? contentLen : bodyLimit;
                unsigned long readStart = millis();
                while (bytesRead < targetBytes) {
                    if (millis() - readStart > static_cast<unsigned long>(timeoutMs)) {
                        Serial.printf("[API] Prefix read timeout after %dms (%d/%d bytes)\n",
                                      timeoutMs, bytesRead, targetBytes);
                        break;
                    }
                    int avail = stream->available();
                    if (avail > 0) {
                        int toRead = avail;
                        if (toRead > targetBytes - bytesRead) {
                            toRead = targetBytes - bytesRead;
                        }
                        int n = stream->readBytes(_rspBuf + bytesRead, toRead);
                        if (n <= 0) break;
                        bytesRead += n;
                        esp_task_wdt_reset();
                    } else if (!stream->connected()) {
                        break;
                    } else {
                        delay(10);
                        esp_task_wdt_reset();
                    }
                }
                Serial.printf("[API] Prefix read %d bytes (limit=%d) %s\n",
                              bytesRead, bodyLimit, url);
            } else if (contentLen > 0 && contentLen < (int)(RSP_BUF_SIZE - 1)) {
                // Known length — raw stream read, WDT-safe
                WiFiClient* stream = http.getStreamPtr();
                unsigned long readStart = millis();
                unsigned long readDeadline = (unsigned long)timeoutMs;
                while (bytesRead < contentLen) {
                    if (millis() - readStart > readDeadline) {
                        Serial.printf("[API] Body read timeout after %dms (%d/%d bytes)\n",
                                      timeoutMs, bytesRead, contentLen);
                        break;
                    }
                    int avail = stream->available();
                    if (avail > 0) {
                        int toRead = avail;
                        if (toRead > contentLen - bytesRead) toRead = contentLen - bytesRead;
                        int n = stream->readBytes(_rspBuf + bytesRead, toRead);
                        if (n <= 0) break;
                        bytesRead += n;
                        esp_task_wdt_reset();
                    } else if (!stream->connected()) {
                        break;
                    } else {
                        delay(10);
                        esp_task_wdt_reset();
                    }
                }
            } else {
                // Chunked / unknown length — let HTTPClient parse chunk framing.
                // Reading raw from getStreamPtr() would leave chunk-size markers
                // in the buffer and break JSON parsing (the v4.9.7 bug).
                PSRAMBufStream bufStream;
                int written = http.writeToStream(&bufStream);
                esp_task_wdt_reset();
                bytesRead = bufStream.bytesWritten;
                if (written < 0) {
                    Serial.printf("[API] writeToStream error %d (%d bytes) %s\n",
                                  written, bytesRead, url);
                }
            }

            _rspBuf[bytesRead] = '\0';
            http.end();
            if (bytesRead > 0) {
                result = API_OK;
                return _rspBuf;
            }
            Serial.printf("[API] Empty body from %s\n", url);
            result = API_NETWORK_ERROR;
            continue;  // retry
        } else if (code == -1 || code == -11) {
            Serial.printf("[API] Timeout (code %d) from %s\n", code, url);
            result = API_TIMEOUT;
        } else {
            Serial.printf("[API] HTTP %d from %s\n", code, url);
            result = API_NETWORK_ERROR;
        }
        http.end();
    }
    return _rspBuf;  // empty string on failure
}

// ── CoinGecko: BTC price + 1h/24h/7d changes ──
ApiResult fetchBtcPrice(BtcPrice& out) {
    ApiResult result;
    static const char url[] = "https://api.coingecko.com/api/v3/coins/bitcoin?localization=false&tickers=false&community_data=false&developer_data=false&sparkline=false";
    const char* json = apiHttpGet(url, false, result);
    if (!json[0]) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, DeserializationOption::NestingLimit(15));
    if (err) {
        Serial.printf("[API] BTC JSON parse error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonObject md = doc["market_data"];
    out.usd = md["current_price"]["usd"] | 0.0f;
    out.change1h = md["price_change_percentage_1h_in_currency"]["usd"] | 0.0f;
    out.change24h = md["price_change_percentage_24h"] | 0.0f;
    out.change7d = md["price_change_percentage_7d"] | 0.0f;
    out.ath = md["ath"]["usd"] | 0.0f;
    out.athChangePercent = md["ath_change_percentage"]["usd"] | 0.0f;
    out.valid = (out.usd > 0);
    out.lastUpdate = millis();

    Serial.printf("[API] BTC: $%.0f (1h:%.2f%% 24h:%.2f%% 7d:%.2f%% ATH:$%.0f)\n",
                  out.usd, out.change1h, out.change24h, out.change7d, out.ath);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── CoinGecko: Lightweight BTC price (for real-time mode) ──
ApiResult fetchBtcPriceSimple(BtcPrice& out) {
    ApiResult result;
    const char* json = apiHttpGet(COINGECKO_SIMPLE_EP, false, result);
    if (!json[0]) return result;

    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        Serial.println("[API] Simple BTC JSON parse error");
        return API_PARSE_ERROR;
    }

    JsonObject btc = doc["bitcoin"];
    float price = btc["usd"] | 0.0f;
    if (price <= 0) return API_PARSE_ERROR;

    out.usd = price;
    out.change24h = btc["usd_24h_change"] | out.change24h;
    out.valid = true;
    out.lastUpdate = millis();

    Serial.printf("[API] BTC simple: $%.0f\n", out.usd);
    return API_OK;
}

// ── CoinGecko: Global market data ──
ApiResult fetchGlobalData(CryptoGlobal& out) {
    ApiResult result;
    const char* json = apiHttpGet(COINGECKO_GLOBAL_EP, false, result);
    if (!json[0]) return result;

    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        Serial.println("[API] Global JSON parse error");
        return API_PARSE_ERROR;
    }

    JsonObject data = doc["data"];
    out.btcDominance = data["market_cap_percentage"]["btc"] | 0.0f;
    out.ethDominance = data["market_cap_percentage"]["eth"] | 0.0f;
    out.totalMarketCapChangePercent24h = data["market_cap_change_percentage_24h_usd"] | 0.0f;
    out.totalVolumeChangePercent24h = 0.0f;
    out.valid = (out.btcDominance > 0);
    out.lastUpdate = millis();

    Serial.printf("[API] Global: BTC dom=%.1f%% ETH dom=%.1f%% MCap chg=%.2f%%\n",
                  out.btcDominance, out.ethDominance, out.totalMarketCapChangePercent24h);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── CoinGecko: Sparkline (downsample to SPARKLINE_POINTS) ──
ApiResult fetchSparkline(SparklineData& out, int days, CoinId coin) {
    const char* geckoId = "bitcoin";
    switch (coin) {
        case COIN_ETH: geckoId = "ethereum"; break;
        case COIN_SOL: geckoId = "solana";   break;
        default:       geckoId = "bitcoin";  break;
    }

    static char urlBuf[256];  // static: off the 8KB stack
    snprintf(urlBuf, sizeof(urlBuf), COINGECKO_CHART_EP_FMT "%d", geckoId, days);

    // Use the shared apiHttpGet helper (reads full response into PSRAM buffer)
    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    // Parse JSON from String with filter (only keep "prices")
    JsonDocument filter;
    filter["prices"][0][0] = true;
    filter["prices"][0][1] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(15));

    if (err) {
        Serial.printf("[API] Sparkline JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray prices = doc["prices"];
    int total = prices.size();
    Serial.printf("[API] Sparkline parsed: %d points\n", total);
    if (total == 0) return API_PARSE_ERROR;

    int targetCount = min((int)SPARKLINE_POINTS, total);
    out.count = targetCount;
    out.minVal = 1e12;
    out.maxVal = -1e12;

    float step = (float)total / targetCount;
    for (int i = 0; i < targetCount; i++) {
        int idx = (int)(i * step);
        if (idx >= total) idx = total - 1;
        float val = prices[idx][1].as<float>();
        out.points[i] = val;
        if (val < out.minVal) out.minVal = val;
        if (val > out.maxVal) out.maxVal = val;
    }

    out.valid = true;
    out.lastUpdate = millis();
    Serial.printf("[API] Sparkline %s (%dd): %d pts, $%.0f-$%.0f\n",
                  geckoId, days, out.count, out.minVal, out.maxVal);
    return API_OK;
}

// ── CoinGecko: Multi-coin market data (single request for all coins) ──
ApiResult fetchMarketData(MarketData& out) {
    ApiResult result;
    const char* json = apiHttpGet(COINGECKO_MARKETS_EP, false, result);
    if (!json[0]) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, DeserializationOption::NestingLimit(15));
    if (err) {
        Serial.printf("[API] Markets JSON parse error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    // Map CoinGecko IDs to our CoinId enum
    struct IdMap { const char* geckoId; CoinId coinId; const char* symbol; };
    static const IdMap idMap[] = {
        { "bitcoin",  COIN_BTC,  "BTC"  },
        { "ethereum", COIN_ETH,  "ETH"  },
        { "solana",   COIN_SOL,  "SOL"  },
        { "tether",   COIN_USDT, "USDT" },
        { "usd-coin", COIN_USDC, "USDC" },
    };

    // Clear validity
    for (int i = 0; i < COIN_COUNT; i++) {
        out.coins[i].valid = false;
    }

    for (JsonObject coin : arr) {
        const char* id = coin["id"] | "";
        for (const auto& m : idMap) {
            if (strcmp(id, m.geckoId) == 0) {
                CoinData& c = out.coins[m.coinId];
                strncpy(c.symbol, m.symbol, sizeof(c.symbol) - 1);
                c.symbol[sizeof(c.symbol) - 1] = '\0';
                c.priceUsd  = coin["current_price"] | 0.0f;
                c.change1h  = coin["price_change_percentage_1h_in_currency"] | 0.0f;
                c.change24h = coin["price_change_percentage_24h_in_currency"] | 0.0f;
                c.change7d  = coin["price_change_percentage_7d_in_currency"] | 0.0f;
                c.marketCap = coin["market_cap"] | 0.0f;
                c.valid     = (c.priceUsd > 0);

                Serial.printf("[API] %s: $%.2f (1h:%.2f%% 24h:%.2f%%)\n",
                              c.symbol, c.priceUsd, c.change1h, c.change24h);
                break;
            }
        }
    }

    out.lastUpdate = millis();

    // Consider success if at least BTC parsed
    return out.coins[COIN_BTC].valid ? API_OK : API_PARSE_ERROR;
}

// ── Binance: Kline data for sparkline (24h, 7d views) ──
ApiResult fetchBinanceKlines(SparklineData& out, const char* interval, int limit) {
    static char urlBuf[128];
    snprintf(urlBuf, sizeof(urlBuf),
             "%s?symbol=BTCUSDT&interval=%s&limit=%d",
             BINANCE_KLINES_EP, interval, limit);

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[API] Binance klines JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    int total = arr.size();
    int targetCount = (total <= SPARKLINE_POINTS) ? total : SPARKLINE_POINTS;

    out.count = targetCount;
    out.minVal = 1e12f;
    out.maxVal = -1e12f;

    float step = (float)total / targetCount;
    int i = 0;
    for (int t = 0; t < targetCount; t++) {
        int idx = (int)(t * step);
        if (idx >= total) idx = total - 1;
        JsonArray kline = arr[idx];
        const char* closeStr = kline[4].as<const char*>();
        float val = closeStr ? atof(closeStr) : 0.0f;
        if (val <= 0) continue;
        out.points[i] = val;
        if (val < out.minVal) out.minVal = val;
        if (val > out.maxVal) out.maxVal = val;
        i++;
    }
    out.count = i;

    out.valid = (i >= 2);
    out.lastUpdate = millis();

    Serial.printf("[API] Binance klines (%s, %d): %d pts, $%.0f-$%.0f\n",
                  interval, limit, out.count, out.minVal, out.maxVal);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── Binance: OHLC candlestick data ──
ApiResult fetchBinanceOhlc(OhlcData& out, const char* interval, int limit) {
    static char urlBuf[128];
    snprintf(urlBuf, sizeof(urlBuf),
             "%s?symbol=BTCUSDT&interval=%s&limit=%d",
             BINANCE_KLINES_EP, interval, limit);

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[API] Binance OHLC JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    int total = arr.size();
    int count = (total <= OHLC_MAX_BARS) ? total : OHLC_MAX_BARS;

    out.count = 0;
    out.minVal = 1e12f;
    out.maxVal = -1e12f;

    for (int i = 0; i < count; i++) {
        JsonArray kline = arr[i];
        // Binance kline indices: 1=open, 2=high, 3=low, 4=close
        const char* openStr  = kline[1].as<const char*>();
        const char* highStr  = kline[2].as<const char*>();
        const char* lowStr   = kline[3].as<const char*>();
        const char* closeStr = kline[4].as<const char*>();

        float o = openStr  ? atof(openStr)  : 0.0f;
        float h = highStr  ? atof(highStr)  : 0.0f;
        float l = lowStr   ? atof(lowStr)   : 0.0f;
        float c = closeStr ? atof(closeStr) : 0.0f;

        if (o <= 0 || h <= 0 || l <= 0 || c <= 0) continue;

        out.bars[out.count] = { o, h, l, c };
        if (l < out.minVal) out.minVal = l;
        if (h > out.maxVal) out.maxVal = h;
        out.count++;
    }

    out.valid = (out.count >= 2);
    out.lastUpdate = millis();

    Serial.printf("[API] Binance OHLC (%s, %d): %d bars, $%.0f-$%.0f\n",
                  interval, limit, out.count, out.minVal, out.maxVal);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── CoinGecko: USDC/ARS sparkline (for dollar chart) ──
ApiResult fetchLemonSparkline(SparklineData& out, int days) {
    // precision=2 reduces response size ~40% (ARS prices don't need 15 decimals)
    static char urlBuf[160];
    snprintf(urlBuf, sizeof(urlBuf), "%s%d&precision=2", COINGECKO_TETHER_CHART_EP, days);

    // Longer timeout for large periods (90d+ = hourly data, big response)
    int timeout = (days > 30) ? 15000 : 10000;
    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result, timeout);
    if (result != API_OK) return result;

    Serial.printf("[API] Lemon sparkline response: %d bytes\n", (int)strlen(json));

    JsonDocument filter;
    filter["prices"][0][0] = true;
    filter["prices"][0][1] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(15));
    if (err) {
        Serial.printf("[API] Lemon sparkline JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray prices = doc["prices"];
    int total = prices.size();
    Serial.printf("[API] Lemon sparkline parsed: %d points for %dd\n", total, days);
    if (total == 0) return API_PARSE_ERROR;

    // Build into temp to avoid corrupting `out` on bad data
    // For longer periods (30d+), cap points for smoother chart (hourly data gets noisy)
    int maxTarget = (days >= 30) ? 180 : (int)SPARKLINE_POINTS;
    int targetCount = min(maxTarget, total);
    float tempMin = 1e12f, tempMax = -1e12f;
    float step = (float)total / targetCount;
    float lastGood = 0;
    static float tempPoints[SPARKLINE_POINTS];

    for (int i = 0; i < targetCount; i++) {
        int idx = (int)(i * step);
        if (idx >= total) idx = total - 1;
        float val = prices[idx][1].as<float>();
        if (!isfinite(val) || val <= 0) val = lastGood;
        tempPoints[i] = val;
        lastGood = val;
        if (val > 0 && val < tempMin) tempMin = val;
        if (val > 0 && val > tempMax) tempMax = val;
    }

    if (tempMax <= 0) {
        Serial.printf("[API] Lemon sparkline %dd: all data invalid\n", days);
        return API_PARSE_ERROR;
    }

    // Data is valid — commit to output
    out.count = targetCount;
    out.minVal = tempMin;
    out.maxVal = tempMax;
    memcpy(out.points, tempPoints, targetCount * sizeof(float));
    out.valid = true;
    out.lastUpdate = millis();
    Serial.printf("[API] Lemon sparkline (%dd): %d pts, $%.0f-$%.0f\n",
                  days, out.count, out.minVal, out.maxVal);
    return API_OK;
}

// ── Binance: Kline data with parameterized symbol (for pair switching) ──
ApiResult fetchBinanceKlinesSymbol(SparklineData& out, const char* symbol,
                                    const char* interval, int limit, bool invert) {
    static char urlBuf[128];
    snprintf(urlBuf, sizeof(urlBuf),
             "%s?symbol=%s&interval=%s&limit=%d",
             BINANCE_KLINES_EP, symbol, interval, limit);

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[API] Binance klines JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    int total = arr.size();
    int targetCount = (total <= SPARKLINE_POINTS) ? total : SPARKLINE_POINTS;

    out.count = targetCount;
    out.minVal = 1e12f;
    out.maxVal = -1e12f;

    float step = (float)total / targetCount;
    int i = 0;
    for (int t = 0; t < targetCount; t++) {
        int idx = (int)(t * step);
        if (idx >= total) idx = total - 1;
        JsonArray kline = arr[idx];
        const char* closeStr = kline[4].as<const char*>();
        float val = closeStr ? atof(closeStr) : 0.0f;
        if (val <= 0) continue;
        if (invert) val = 1.0f / val;
        out.points[i] = val;
        if (val < out.minVal) out.minVal = val;
        if (val > out.maxVal) out.maxVal = val;
        i++;
    }
    out.count = i;

    out.valid = (i >= 2);
    out.lastUpdate = millis();

    Serial.printf("[API] Binance klines %s (%s, %d): %d pts, %.4f-%.4f\n",
                  symbol, interval, limit, out.count, out.minVal, out.maxVal);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── Binance: OHLC with parameterized symbol ──
ApiResult fetchBinanceOhlcSymbol(OhlcData& out, const char* symbol,
                                  const char* interval, int limit, bool invert) {
    static char urlBuf[128];
    snprintf(urlBuf, sizeof(urlBuf),
             "%s?symbol=%s&interval=%s&limit=%d",
             BINANCE_KLINES_EP, symbol, interval, limit);

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[API] Binance OHLC JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    int total = arr.size();
    int count = (total <= OHLC_MAX_BARS) ? total : OHLC_MAX_BARS;

    out.count = 0;
    out.minVal = 1e12f;
    out.maxVal = -1e12f;

    for (int i = 0; i < count; i++) {
        JsonArray kline = arr[i];
        const char* openStr  = kline[1].as<const char*>();
        const char* highStr  = kline[2].as<const char*>();
        const char* lowStr   = kline[3].as<const char*>();
        const char* closeStr = kline[4].as<const char*>();

        float o = openStr  ? atof(openStr)  : 0.0f;
        float h = highStr  ? atof(highStr)  : 0.0f;
        float l = lowStr   ? atof(lowStr)   : 0.0f;
        float c = closeStr ? atof(closeStr) : 0.0f;

        if (o <= 0 || h <= 0 || l <= 0 || c <= 0) continue;

        if (invert) {
            // Invert all OHLC values; note high/low swap when inverting
            float io = 1.0f / o;
            float ih = 1.0f / l;   // 1/low becomes high
            float il = 1.0f / h;   // 1/high becomes low
            float ic = 1.0f / c;
            o = io; h = ih; l = il; c = ic;
        }

        out.bars[out.count] = { o, h, l, c };
        if (l < out.minVal) out.minVal = l;
        if (h > out.maxVal) out.maxVal = h;
        out.count++;
    }

    out.valid = (out.count >= 2);
    out.lastUpdate = millis();

    Serial.printf("[API] Binance OHLC %s (%s, %d): %d bars\n",
                  symbol, interval, limit, out.count);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── CoinGecko: Sparkline with configurable vs_currency ──
ApiResult fetchSparklineVsCurrency(SparklineData& out, int days,
                                    const char* vsCurrency, CoinId coin) {
    const char* geckoId = "bitcoin";
    switch (coin) {
        case COIN_ETH: geckoId = "ethereum"; break;
        case COIN_SOL: geckoId = "solana";   break;
        default:       geckoId = "bitcoin";  break;
    }

    static char urlBuf[256];  // static: off the 8KB stack
    snprintf(urlBuf, sizeof(urlBuf),
             "https://api.coingecko.com/api/v3/coins/%s/market_chart?vs_currency=%s&days=%d",
             geckoId, vsCurrency, days);

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument filter;
    filter["prices"][0][0] = true;
    filter["prices"][0][1] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(15));

    if (err) {
        Serial.printf("[API] Sparkline JSON error: %s\n", err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArray prices = doc["prices"];
    int total = prices.size();
    if (total == 0) return API_PARSE_ERROR;

    int targetCount = min((int)SPARKLINE_POINTS, total);
    out.count = targetCount;
    out.minVal = 1e12;
    out.maxVal = -1e12;

    float step = (float)total / targetCount;
    for (int i = 0; i < targetCount; i++) {
        int idx = (int)(i * step);
        if (idx >= total) idx = total - 1;
        float val = prices[idx][1].as<float>();
        out.points[i] = val;
        if (val < out.minVal) out.minVal = val;
        if (val > out.maxVal) out.maxVal = val;
    }

    out.valid = true;
    out.lastUpdate = millis();
    Serial.printf("[API] Sparkline %s vs %s (%dd): %d pts, %.4f-%.4f\n",
                  geckoId, vsCurrency, days, out.count, out.minVal, out.maxVal);
    return API_OK;
}

// ── CoinGecko: Fetch BTC price in arbitrary currency (for XAU, etc.) ──
ApiResult fetchGeckoBtcPrice(const char* vsCurrency, float& outPrice) {
    static char urlBuf[128];
    snprintf(urlBuf, sizeof(urlBuf),
             "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=%s",
             vsCurrency);

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        return API_PARSE_ERROR;
    }

    float price = doc["bitcoin"][vsCurrency] | 0.0f;
    if (price <= 0) return API_PARSE_ERROR;

    outPrice = price;
    Serial.printf("[API] BTC/%s: %.4f\n", vsCurrency, outPrice);
    return API_OK;
}

// ── CriptoYa: Lemon USDC/ARS price ──
ApiResult fetchLemonPrice(LemonPrice& out) {
    ApiResult result;
    const char* json = apiHttpGet(CRIPTOYA_LEMON_EP, false, result);
    if (!json[0]) return result;

    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        Serial.println("[API] Lemon JSON parse error");
        return API_PARSE_ERROR;
    }

    out.ask = doc["totalAsk"] | 0.0f;
    out.bid = doc["totalBid"] | 0.0f;
    out.valid = (out.ask > 0 && out.bid > 0);
    out.lastUpdate = millis();

    Serial.printf("[API] Lemon USDC/ARS: bid=%.2f ask=%.2f\n", out.bid, out.ask);
    return out.valid ? API_OK : API_PARSE_ERROR;
}

// ── Polymarket: Fetch BTC prediction markets mapped to chart timeframe ──
static bool containsCI(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    size_t hLen = strlen(haystack), nLen = strlen(needle);
    if (nLen > hLen) return false;
    for (size_t i = 0; i <= hLen - nLen; i++) {
        bool match = true;
        for (size_t j = 0; j < nLen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static bool isBtcMarket(const char* question, const char* slug) {
    return containsCI(question, "bitcoin") || containsCI(question, "btc") ||
           containsCI(slug, "bitcoin") || containsCI(slug, "btc");
}

static bool isPricePredictionQuestion(const char* question, const char* slug) {
    return containsCI(slug, "btc-updown") ||
           containsCI(question, "up or down") ||
           containsCI(question, "reach") ||
           containsCI(question, "above") ||
           containsCI(question, "below") ||
           containsCI(question, "price");
}

static float parseFloatVar(JsonVariantConst v) {
    if (v.is<float>() || v.is<double>() || v.is<int>() || v.is<long>() || v.is<unsigned long>()) {
        return v.as<float>();
    }
    const char* s = v.as<const char*>();
    return s ? atof(s) : 0.0f;
}

static bool parseOutcomePrices(JsonVariantConst pricesVar, float& yes, float& no) {
    yes = 0.0f;
    no = 0.0f;

    JsonArrayConst pa = pricesVar.as<JsonArrayConst>();
    if (!pa.isNull() && pa.size() >= 2) {
        yes = parseFloatVar(pa[0]);
        no  = parseFloatVar(pa[1]);
        return (yes > 0.0f || no > 0.0f);
    }

    const char* pricesStr = pricesVar.as<const char*>();
    if (pricesStr && pricesStr[0] == '[') {
        JsonDocument pricesDoc;
        if (!deserializeJson(pricesDoc, pricesStr)) {
            JsonArrayConst pa2 = pricesDoc.as<JsonArrayConst>();
            if (pa2.size() >= 2) {
                yes = parseFloatVar(pa2[0]);
                no  = parseFloatVar(pa2[1]);
                return (yes > 0.0f || no > 0.0f);
            }
        }
    }
    return false;
}

static bool equalsYesNo(const char* s, bool& yesValue) {
    if (!s || !s[0]) return false;

    char buf[8];
    size_t n = strlen(s);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        buf[i] = c;
    }
    buf[n] = '\0';

    if (strcmp(buf, "yes") == 0) {
        yesValue = true;
        return true;
    }
    if (strcmp(buf, "no") == 0) {
        yesValue = false;
        return true;
    }
    return false;
}

static bool parseResolvedOutcome(JsonObjectConst m, bool& yesWon) {
    const char* resolvedOutcome = m["resolvedOutcome"] | "";
    if (equalsYesNo(resolvedOutcome, yesWon)) return true;

    const char* winningOutcome = m["winningOutcome"] | "";
    if (equalsYesNo(winningOutcome, yesWon)) return true;

    const char* winner = m["winner"] | "";
    if (equalsYesNo(winner, yesWon)) return true;

    return false;
}

static bool parseWinnerFromTokens(JsonVariantConst tokensVar, bool& yesWon) {
    JsonArrayConst tokens = tokensVar.as<JsonArrayConst>();
    if (tokens.isNull() || tokens.size() == 0) return false;

    for (JsonObjectConst token : tokens) {
        bool winner = token["winner"] | false;
        if (!winner) continue;
        const char* outcome = token["outcome"] | "";
        if (equalsYesNo(outcome, yesWon)) return true;
    }
    return false;
}

static bool parsePolyMarket(JsonObjectConst m, PolyMarket& pm) {
    memset(&pm, 0, sizeof(PolyMarket));

    const char* question = m["question"] | "";
    strncpy(pm.question, question, PM_QUESTION_LEN - 1);
    pm.question[PM_QUESTION_LEN - 1] = '\0';

    const char* condId = m["conditionId"] | "";
    strncpy(pm.conditionId, condId, PM_COND_ID_LEN - 1);
    pm.conditionId[PM_COND_ID_LEN - 1] = '\0';

    bool hasOutcomePrices = parseOutcomePrices(m["outcomePrices"], pm.yesPrice, pm.noPrice);

    if (!hasOutcomePrices) {
        float lastTrade = parseFloatVar(m["lastTradePrice"]);
        float bestBid   = parseFloatVar(m["bestBid"]);
        float bestAsk   = parseFloatVar(m["bestAsk"]);

        float yes = 0.0f;
        if (lastTrade > 0.0f && lastTrade < 1.0f) {
            yes = lastTrade;
        } else if (bestBid > 0.0f && bestBid < 1.0f && bestAsk > 0.0f && bestAsk < 1.0f) {
            yes = (bestBid + bestAsk) * 0.5f;
        } else if (bestBid > 0.0f && bestBid < 1.0f) {
            yes = bestBid;
        } else if (bestAsk > 0.0f && bestAsk < 1.0f) {
            yes = bestAsk;
        }

        if (yes > 0.0f && yes < 1.0f) {
            pm.yesPrice = yes;
            pm.noPrice = 1.0f - yes;
        }
    }

    pm.volume24hr = parseFloatVar(m["volume24hr"]);
    // Event-slug markets lack volume24hr; fall back to total volume
    if (pm.volume24hr <= 0.0f) {
        pm.volume24hr = parseFloatVar(m["volume"]);
    }

    const char* startTimeStr = m["eventStartTime"] | "";
    if (!startTimeStr || startTimeStr[0] == '\0') {
        startTimeStr = m["startDate"] | "";
    }
    strncpy(pm.startTime, startTimeStr, sizeof(pm.startTime) - 1);
    pm.startTime[sizeof(pm.startTime) - 1] = '\0';

    const char* endDateStr = m["endDate"] | "";
    strncpy(pm.endDate, endDateStr, sizeof(pm.endDate) - 1);
    pm.endDate[sizeof(pm.endDate) - 1] = '\0';
    pm.refPrice = 0.0f;
    pm.refPriceValid = false;

    pm.closed = m["closed"] | false;
    pm.winnerKnown = parseResolvedOutcome(m, pm.yesWon) ||
                     parseWinnerFromTokens(m["tokens"], pm.yesWon);
    bool hasPrices = (pm.yesPrice >= 0.0f && pm.noPrice >= 0.0f &&
                     (pm.yesPrice > 0.0f || pm.noPrice > 0.0f));
    pm.valid = pm.conditionId[0] != '\0' && (hasPrices || pm.winnerKnown || pm.closed);
    return pm.valid;
}

// ── Reference price via Binance kline (1m candle open at interval start) ──
// Much more reliable than Chainlink scraping — 1-minute resolution for all timeframes.

// Simple cache: avoid re-fetching the same startTime
static const uint8_t REF_CACHE_SIZE = 8;
struct RefCacheEntry {
    char startTime[32];
    float price;
    bool valid;
};
static RefCacheEntry refCache[REF_CACHE_SIZE] = {};
static uint8_t refCacheCount = 0;
static uint8_t refCacheHead = 0;

static bool refCacheLookup(const char* startTime, float& outPrice) {
    for (uint8_t i = 0; i < refCacheCount; i++) {
        if (strcmp(refCache[i].startTime, startTime) == 0) {
            if (refCache[i].valid) { outPrice = refCache[i].price; return true; }
            return false;
        }
    }
    return false;
}

static void refCacheStore(const char* startTime, float price, bool valid) {
    if (!startTime || !startTime[0]) return;
    for (uint8_t i = 0; i < refCacheCount; i++) {
        if (strcmp(refCache[i].startTime, startTime) == 0) {
            refCache[i].price = price;
            refCache[i].valid = valid;
            return;
        }
    }
    uint8_t idx = (refCacheCount < REF_CACHE_SIZE) ? refCacheCount++ : refCacheHead;
    if (refCacheCount > REF_CACHE_SIZE) {
        refCacheHead = (uint8_t)((refCacheHead + 1) % REF_CACHE_SIZE);
    }
    strncpy(refCache[idx].startTime, startTime, sizeof(refCache[idx].startTime) - 1);
    refCache[idx].startTime[sizeof(refCache[idx].startTime) - 1] = '\0';
    refCache[idx].price = price;
    refCache[idx].valid = valid;
}

// Parse ISO 8601 "YYYY-MM-DDTHH:MM:SSZ" → UTC epoch seconds
// Manual calculation — avoids mktime timezone issues entirely.
uint32_t isoToEpoch(const char* iso) {
    if (!iso || strlen(iso) < 19) return 0;
    int yr, mo, dy, hr, mn, sc;
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc) < 6) return 0;
    if (yr < 1970 || mo < 1 || mo > 12 || dy < 1) return 0;

    static const int mdays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t days = 0;
    for (int y = 1970; y < yr; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }
    for (int m = 1; m < mo; m++) {
        days += mdays[m];
        if (m == 2 && (yr % 4 == 0 && (yr % 100 != 0 || yr % 400 == 0))) days++;
    }
    days += dy - 1;
    return days * 86400UL + hr * 3600UL + mn * 60UL + sc;
}

static ApiResult fetchBinanceOpenPriceAtInternal(uint32_t epochSec, float& outPrice) {
    outPrice = 0.0f;
    if (epochSec == 0) return API_PARSE_ERROR;

    static char urlBuf[160];
    snprintf(urlBuf, sizeof(urlBuf),
              "https://api.binance.com/api/v3/klines?symbol=BTCUSDT&interval=1m&startTime=%lu000&limit=1",
              (unsigned long)epochSec);

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result);
    if (result != API_OK || !json[0]) return result;

    // Response: [[openTime,"open","high","low","close",...]]
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return API_PARSE_ERROR;

    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    JsonArrayConst candle = arr[0].as<JsonArrayConst>();
    if (candle.isNull() || candle.size() < 5) return API_PARSE_ERROR;

    // Index 1 = open price (string)
    const char* openStr = candle[1] | "";
    float openPrice = atof(openStr);
    if (openPrice <= 0.0f) return API_PARSE_ERROR;

    outPrice = openPrice;
    return API_OK;
}

static bool fetchBinanceRefPrice(const char* startTime, float& outPrice) {
    if (!startTime || !startTime[0]) return false;

    // Check cache first
    if (refCacheLookup(startTime, outPrice)) return true;

    uint32_t epochSec = isoToEpoch(startTime);
    if (epochSec == 0) {
        refCacheStore(startTime, 0.0f, false);
        return false;
    }

    ApiResult result = fetchBinanceOpenPriceAtInternal(epochSec, outPrice);
    if (result != API_OK) {
        refCacheStore(startTime, 0.0f, false);
        return false;
    }

    refCacheStore(startTime, outPrice, true);
    Serial.printf("[API] Binance ref: start=%s epoch=%lu open=%.2f\n",
                  startTime, (unsigned long)epochSec, outPrice);
    return true;
}

ApiResult fetchBinanceOpenPriceAt(uint32_t epochSec, float& outPrice) {
    return fetchBinanceOpenPriceAtInternal(epochSec, outPrice);
}

void enrichPolyReference(PolyMarket& pm) {
    pm.refPrice = 0.0f;
    pm.refPriceValid = false;
    if (pm.startTime[0] == '\0') {
        Serial.println("[API] enrichPoly: startTime is empty, skipping");
        return;
    }

    Serial.printf("[API] enrichPoly: startTime=%s\n", pm.startTime);
    float ref = 0.0f;
    if (fetchBinanceRefPrice(pm.startTime, ref) && ref > 0.0f) {
        pm.refPrice = ref;
        pm.refPriceValid = true;
        Serial.printf("[API] enrichPoly: OK refPrice=%.2f\n", ref);
    } else {
        Serial.printf("[API] enrichPoly: FAILED for startTime=%s\n", pm.startTime);
    }
}

struct PolyUpDownSpec {
    const char* tf;
    uint32_t stepSec;
    uint32_t offsetSec;
};

static bool getUpDownSpecForPeriod(uint8_t btcPeriod, PolyUpDownSpec& spec) {
    switch (btcPeriod) {
        case 0: spec = { "5m",   300,   0 }; break;
        case 1: spec = { "15m",  900,   0 }; break;
        // Only 5m and 15m markets exist on Polymarket as of 2026-03
        default: return false;
    }
    return true;
}

// Extract Unix timestamp from btc-updown slug and write ISO 8601 into startTime.
// Slug format: "btc-updown-{tf}-{unix_ts}"  e.g. "btc-updown-5m-1739984400"
// The eventStartTime field from the API is the event CREATION date — NOT the
// interval start — which can be hours/days earlier.  The slug timestamp IS the
// correct interval start and must be used for the Chainlink reference lookup.
static void overrideStartTimeFromSlug(const char* slug, PolyMarket& pm) {
    if (!slug) return;
    // Find the last '-' to locate the timestamp portion
    const char* last = strrchr(slug, '-');
    if (!last || *(last + 1) == '\0') return;
    int64_t ts = strtoll(last + 1, nullptr, 10);
    if (ts < 1700000000) return;  // sanity: must be after 2023
    time_t t = (time_t)ts;
    struct tm utc;
    gmtime_r(&t, &utc);
    snprintf(pm.startTime, sizeof(pm.startTime),
             "%04d-%02d-%02dT%02d:%02d:%02dZ",
             utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
             utc.tm_hour, utc.tm_min, utc.tm_sec);
}

static ApiResult fetchPolyFromEventSlug(const char* slug, PolyMarket* out, uint8_t& count) {
    count = 0;

    static char urlBuf[220];
    snprintf(urlBuf, sizeof(urlBuf),
             "https://gamma-api.polymarket.com/events?slug=%s",
             slug);

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument filter;
    filter[0]["markets"][0]["question"] = true;
    filter[0]["markets"][0]["slug"] = true;
    filter[0]["markets"][0]["conditionId"] = true;
    filter[0]["markets"][0]["outcomePrices"] = true;
    filter[0]["markets"][0]["lastTradePrice"] = true;
    filter[0]["markets"][0]["bestBid"] = true;
    filter[0]["markets"][0]["bestAsk"] = true;
    filter[0]["markets"][0]["volume24hr"] = true;
    filter[0]["markets"][0]["volume"] = true;
    filter[0]["markets"][0]["endDate"] = true;
    filter[0]["markets"][0]["eventStartTime"] = true;
    filter[0]["markets"][0]["startDate"] = true;
    filter[0]["markets"][0]["closed"] = true;
    filter[0]["markets"][0]["resolvedOutcome"] = true;
    filter[0]["markets"][0]["winningOutcome"] = true;
    filter[0]["markets"][0]["winner"] = true;
    filter[0]["markets"][0]["tokens"][0]["winner"] = true;
    filter[0]["markets"][0]["tokens"][0]["outcome"] = true;
    filter[0]["markets"][0]["tokens"][1]["winner"] = true;
    filter[0]["markets"][0]["tokens"][1]["outcome"] = true;
    filter[0]["startTime"] = true;
    filter[0]["startDate"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(12));
    if (err) {
        Serial.printf("[API] Polymarket event parse error (%s): %s\n", slug, err.c_str());
        return API_PARSE_ERROR;
    }

    JsonArrayConst events = doc.as<JsonArrayConst>();
    if (events.isNull() || events.size() == 0) return API_PARSE_ERROR;

    JsonArrayConst markets = events[0]["markets"].as<JsonArrayConst>();
    if (markets.isNull() || markets.size() == 0) return API_PARSE_ERROR;

    JsonObjectConst m = markets[0].as<JsonObjectConst>();
    if (!parsePolyMarket(m, out[0])) return API_PARSE_ERROR;

    if (out[0].startTime[0] == '\0') {
        const char* evtStart = events[0]["startTime"] | "";
        if (!evtStart || evtStart[0] == '\0') {
            evtStart = events[0]["startDate"] | "";
        }
        strncpy(out[0].startTime, evtStart, sizeof(out[0].startTime) - 1);
        out[0].startTime[sizeof(out[0].startTime) - 1] = '\0';
    }

    count = 1;
    return API_OK;
}

static ApiResult fetchPolyUpDownForPeriod(PolyMarket* out, uint8_t& count, uint8_t btcPeriod) {
    count = 0;

    PolyUpDownSpec spec = {};
    if (!getUpDownSpecForPeriod(btcPeriod, spec)) return API_PARSE_ERROR;

    time_t now = time(nullptr);
    if (now < 1700000000) {
        Serial.println("[API] Polymarket up/down: invalid epoch (NTP not ready?)");
        return API_PARSE_ERROR;
    }

    int64_t baseTs = ((int64_t)now - (int64_t)spec.offsetSec) / (int64_t)spec.stepSec;
    baseTs = baseTs * (int64_t)spec.stepSec + (int64_t)spec.offsetSec;

    Serial.printf("[API] Polymarket up/down: now=%lld base=%lld step=%lu tf=%s\n",
                  (long long)now, (long long)baseTs, (unsigned long)spec.stepSec, spec.tf);

    static const int8_t CANDIDATE_BUCKETS[] = { 0, -1, 1, -2 };
    for (int8_t delta : CANDIDATE_BUCKETS) {
        int64_t ts = baseTs + (int64_t)delta * (int64_t)spec.stepSec;
        char slug[64];
        snprintf(slug, sizeof(slug), "btc-updown-%s-%lld", spec.tf, (long long)ts);

        ApiResult res = fetchPolyFromEventSlug(slug, out, count);
        if (res == API_OK && count > 0) {
            overrideStartTimeFromSlug(slug, out[0]);
            Serial.printf("[API] Polymarket up/down match: %s delta=%d start=%s\n",
                          slug, (int)delta, out[0].startTime);
            return API_OK;
        }
        Serial.printf("[API] Polymarket slug miss: %s (delta=%d)\n", slug, (int)delta);
    }

    Serial.printf("[API] Polymarket up/down miss for period idx %d (%s)\n", btcPeriod, spec.tf);
    return API_PARSE_ERROR;
}

static ApiResult fetchPolyUpDownRecent(PolyMarket* out, uint8_t& count, uint8_t btcPeriod) {
    count = 0;

    PolyUpDownSpec spec = {};
    if (!getUpDownSpecForPeriod(btcPeriod, spec)) return API_PARSE_ERROR;

    char prefix[24];
    snprintf(prefix, sizeof(prefix), "btc-updown-%s-", spec.tf);

    // PAGE_LIMIT halved from 20 → 10: a 20-market response was ~104KB,
    // exceeding the 96KB API buffer and silently failing parse. With 10 it
    // lands at ~46KB. MAX_OFFSET bumped to 70 so we still scan 80 markets
    // total (8 pages × 10 = same coverage as old 4 × 20).
    static const uint16_t PAGE_LIMIT = 10;
    static const uint16_t MAX_OFFSET = 70;
    for (uint16_t offset = 0; offset <= MAX_OFFSET; offset += PAGE_LIMIT) {
        esp_task_wdt_reset();
        static char urlBuf[260];  // static: off the 8KB stack
        snprintf(urlBuf, sizeof(urlBuf),
                 "%s?active=true&closed=false&order=createdAt&ascending=false&limit=%u&offset=%u",
                 POLYMARKET_GAMMA_URL, (unsigned)PAGE_LIMIT, (unsigned)offset);

        ApiResult result;
        const char* json = apiHttpGet(urlBuf, false, result);
        if (result != API_OK) return result;

        JsonDocument filter;
        filter[0]["question"] = true;
        filter[0]["slug"] = true;
        filter[0]["conditionId"] = true;
        filter[0]["outcomePrices"] = true;
        filter[0]["lastTradePrice"] = true;
        filter[0]["bestBid"] = true;
        filter[0]["bestAsk"] = true;
        filter[0]["volume24hr"] = true;
        filter[0]["volume"] = true;
        filter[0]["endDate"] = true;
        filter[0]["eventStartTime"] = true;
        filter[0]["startDate"] = true;
        filter[0]["closed"] = true;
        filter[0]["resolvedOutcome"] = true;
        filter[0]["winningOutcome"] = true;
        filter[0]["winner"] = true;
        filter[0]["tokens"][0]["winner"] = true;
        filter[0]["tokens"][0]["outcome"] = true;
        filter[0]["tokens"][1]["winner"] = true;
        filter[0]["tokens"][1]["outcome"] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, json,
            DeserializationOption::Filter(filter),
            DeserializationOption::NestingLimit(10));
        if (err) {
            Serial.printf("[API] Polymarket recent parse error (off=%u): %s\n",
                          (unsigned)offset, err.c_str());
            return API_PARSE_ERROR;
        }

        JsonArrayConst arr = doc.as<JsonArrayConst>();
        if (arr.isNull() || arr.size() == 0) break;

        for (JsonObjectConst m : arr) {
            const char* slug = m["slug"] | "";
            if (strncmp(slug, prefix, strlen(prefix)) != 0) continue;

            static PolyMarket pm;  // static: ~265 bytes off the 8KB stack
            memset(&pm, 0, sizeof(pm));
            if (parsePolyMarket(m, pm)) {
                out[0] = pm;
                overrideStartTimeFromSlug(slug, out[0]);
                count = 1;
                Serial.printf("[API] Polymarket up/down recent match: %s (start=%s)\n", slug, out[0].startTime);
                return API_OK;
            }

            // Fallback: pull event detail for this slug if compact market payload is incomplete.
            ApiResult bySlug = fetchPolyFromEventSlug(slug, out, count);
            if (bySlug == API_OK && count > 0) {
                overrideStartTimeFromSlug(slug, out[0]);
                Serial.printf("[API] Polymarket up/down recent match (event): %s (start=%s)\n", slug, out[0].startTime);
                return API_OK;
            }
        }
    }

    Serial.printf("[API] Polymarket up/down recent miss for period idx %d (%s)\n", btcPeriod, spec.tf);
    return API_PARSE_ERROR;
}

static ApiResult fetchPolyBtcFallback(PolyMarket* out, uint8_t& count, uint8_t limit) {
    static char urlBuf[220];
    // Limit halved from 20 → 10: a 20-market BTC response was ~145KB,
    // exceeding the 96KB API buffer. With 10 it's ~72KB. The top BTC markets
    // by volume24hr still end up in the smaller window.
    snprintf(urlBuf, sizeof(urlBuf),
             "%s?active=true&closed=false&order=volume24hr&ascending=false&limit=10&tag=bitcoin",
             POLYMARKET_GAMMA_URL);

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument filter;
    filter[0]["question"] = true;
    filter[0]["slug"] = true;
    filter[0]["conditionId"] = true;
    filter[0]["outcomePrices"] = true;
    filter[0]["lastTradePrice"] = true;
    filter[0]["bestBid"] = true;
    filter[0]["bestAsk"] = true;
    filter[0]["volume24hr"] = true;
    filter[0]["volume"] = true;
    filter[0]["endDate"] = true;
    filter[0]["eventStartTime"] = true;
    filter[0]["startDate"] = true;
    filter[0]["closed"] = true;
    filter[0]["resolvedOutcome"] = true;
    filter[0]["winningOutcome"] = true;
    filter[0]["winner"] = true;
    filter[0]["tokens"][0]["winner"] = true;
    filter[0]["tokens"][0]["outcome"] = true;
    filter[0]["tokens"][1]["winner"] = true;
    filter[0]["tokens"][1]["outcome"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(10));
    if (err) return API_PARSE_ERROR;

    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    count = 0;

    for (JsonObjectConst m : arr) {
        if (count >= limit) break;
        const char* q = m["question"] | "";
        const char* slug = m["slug"] | "";
        if (!isBtcMarket(q, slug) || !isPricePredictionQuestion(q, slug)) continue;

        static PolyMarket pm;  // static: ~265 bytes off the 8KB stack
        memset(&pm, 0, sizeof(pm));
        if (!parsePolyMarket(m, pm)) continue;
        out[count++] = pm;
    }

    if (count < limit) {
        for (JsonObjectConst m : arr) {
            if (count >= limit) break;
            const char* q = m["question"] | "";
            const char* slug = m["slug"] | "";
            if (!isBtcMarket(q, slug) || isPricePredictionQuestion(q, slug)) continue;

            static PolyMarket pm;  // static: ~265 bytes off the 8KB stack
            memset(&pm, 0, sizeof(pm));
            if (!parsePolyMarket(m, pm)) continue;

            bool dup = false;
            for (uint8_t i = 0; i < count; i++) {
                if (strcmp(out[i].conditionId, pm.conditionId) == 0) {
                    dup = true;
                    break;
                }
            }
            if (!dup) out[count++] = pm;
        }
    }

    Serial.printf("[API] Polymarket fallback BTC markets: %d\n", count);
    return (count > 0) ? API_OK : API_PARSE_ERROR;
}

ApiResult fetchPolyMarkets(PolyMarket* out, uint8_t& count, uint8_t limit, uint8_t btcPeriod) {
    count = 0;
    if (!out || limit == 0) return API_PARSE_ERROR;
    if (limit > PM_MAX_MARKETS) limit = PM_MAX_MARKETS;

    // Only periods 0 (5m) and 1 (15m) have up/down markets; others go straight to fallback
    PolyUpDownSpec spec = {};
    if (getUpDownSpecForPeriod(btcPeriod, spec)) {
        ApiResult tsRes = fetchPolyUpDownForPeriod(out, count, btcPeriod);
        if (tsRes == API_OK && count > 0) {
            return API_OK;
        }

        // Timestamp-based lookup failed — try paginated brute-force search by slug prefix
        Serial.printf("[API] Polymarket timestamp miss for period idx=%d, trying recent scan...\n",
                      btcPeriod);
        ApiResult recentRes = fetchPolyUpDownRecent(out, count, btcPeriod);
        if (recentRes == API_OK && count > 0) {
            return API_OK;
        }
        Serial.printf("[API] Polymarket recent miss too for up/down period idx=%d\n",
                      btcPeriod);
        return API_PARSE_ERROR;
    } else {
        Serial.printf("[API] Polymarket period idx=%d has no up/down markets, using BTC fallback\n",
                      btcPeriod);
    }

    // Generic BTC market fallback (always works if Polymarket is reachable)
    return fetchPolyBtcFallback(out, count, limit);
}

ApiResult fetchPolyMarketByConditionId(const char* conditionId, PolyMarket& out) {
    memset(&out, 0, sizeof(out));
    if (!conditionId || conditionId[0] == '\0') return API_PARSE_ERROR;

    static char urlBuf[280];  // static: off the 8KB stack
    snprintf(urlBuf, sizeof(urlBuf), "%s?condition_ids=%s&limit=1",
             POLYMARKET_GAMMA_URL, conditionId);

    ApiResult result;
    const char* json = apiHttpGet(urlBuf, false, result);
    if (result != API_OK) return result;

    JsonDocument filter;
    filter[0]["question"] = true;
    filter[0]["slug"] = true;
    filter[0]["conditionId"] = true;
    filter[0]["outcomePrices"] = true;
    filter[0]["lastTradePrice"] = true;
    filter[0]["bestBid"] = true;
    filter[0]["bestAsk"] = true;
    filter[0]["volume24hr"] = true;
    filter[0]["volume"] = true;
    filter[0]["endDate"] = true;
    filter[0]["eventStartTime"] = true;
    filter[0]["startDate"] = true;
    filter[0]["closed"] = true;
    filter[0]["resolvedOutcome"] = true;
    filter[0]["winningOutcome"] = true;
    filter[0]["winner"] = true;
    filter[0]["tokens"][0]["winner"] = true;
    filter[0]["tokens"][0]["outcome"] = true;
    filter[0]["tokens"][1]["winner"] = true;
    filter[0]["tokens"][1]["outcome"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(10));
    if (err) return API_PARSE_ERROR;

    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull() || arr.size() == 0) return API_PARSE_ERROR;

    for (JsonObjectConst m : arr) {
        const char* cond = m["conditionId"] | "";
        if (strcmp(cond, conditionId) != 0) continue;
        if (!parsePolyMarket(m, out)) return API_PARSE_ERROR;
        return API_OK;
    }

    return API_PARSE_ERROR;
}

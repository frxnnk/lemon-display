#include "supabase_client.h"
#include "config.h"
#include "nvs_storage.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ── State ──
static WebSocketsClient supaWs;
static bool wsConnected = false;
static PairingState pairingState = PAIRING_IDLE;
static RemoteConfig remoteConfig = {};
static bool configDirty = false;

static char deviceId[48]    = {0};
static char pairingCode[8]  = "------";
static char lemonTag[32]    = {0};

// ── Phoenix protocol state ──
static uint32_t phxRef = 0;
static unsigned long lastPhxHeartbeat = 0;
static unsigned long lastReconnectAttempt = 0;
static uint32_t reconnectDelay = SUPA_RECONNECT_BASE_MS;
static bool joinedChannel = false;

// ── Helper: get ESP32 MAC as hardware_id ──
static String getHardwareId() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// ── Phoenix channel: send join message ──
static void sendPhxJoin() {
    if (!wsConnected || deviceId[0] == '\0') return;

    JsonDocument doc;
    doc["topic"] = String("realtime:device_config_") + deviceId;
    doc["event"] = "phx_join";
    doc["payload"]["config"]["postgres_changes"][0]["event"] = "*";
    doc["payload"]["config"]["postgres_changes"][0]["schema"] = "public";
    doc["payload"]["config"]["postgres_changes"][0]["table"] = "device_config";
    doc["payload"]["config"]["postgres_changes"][0]["filter"] = String("device_id=eq.") + deviceId;
    doc["ref"] = String(++phxRef);

    String msg;
    serializeJson(doc, msg);
    supaWs.sendTXT(msg);
    Serial.printf("[Supa] Sent phx_join for device %s\n", deviceId);
}

// ── Phoenix channel: send heartbeat ──
static void sendPhxHeartbeat() {
    if (!wsConnected) return;

    JsonDocument doc;
    doc["topic"] = "phoenix";
    doc["event"] = "heartbeat";
    doc["payload"] = JsonObject();
    doc["ref"] = String(++phxRef);

    String msg;
    serializeJson(doc, msg);
    supaWs.sendTXT(msg);
}

// ── Parse incoming postgres_changes for device_config ──
static void parseConfigChange(JsonObject& payload) {
    JsonObject record = payload["record"];
    if (record.isNull()) return;

    if (record["brightness"].is<int>())
        remoteConfig.brightness = record["brightness"].as<uint8_t>();
    if (record["layout_preset"].is<int>())
        remoteConfig.layoutPreset = record["layout_preset"].as<uint8_t>();
    if (record["selected_pair"].is<int>())
        remoteConfig.selectedPair = record["selected_pair"].as<uint8_t>();
    if (record["selected_period"].is<int>())
        remoteConfig.selectedPeriod = record["selected_period"].as<uint8_t>();
    if (record["dollar_period"].is<int>())
        remoteConfig.dollarPeriod = record["dollar_period"].as<uint8_t>();
    if (record["chart_style"].is<int>())
        remoteConfig.chartStyle = record["chart_style"].as<uint8_t>();
    if (record["sound_enabled"].is<bool>())
        remoteConfig.soundEnabled = record["sound_enabled"].as<bool>();
    if (record["alert_enabled"].is<bool>())
        remoteConfig.alertEnabled = record["alert_enabled"].as<bool>();
    if (record["use_24h"].is<bool>())
        remoteConfig.use24h = record["use_24h"].as<bool>();
    if (record["greeting_text"].is<const char*>()) {
        const char* txt = record["greeting_text"] | "";
        strncpy(remoteConfig.greetingText, txt, sizeof(remoteConfig.greetingText) - 1);
    }
    if (record["show_portfolio"].is<bool>())
        remoteConfig.showPortfolio = record["show_portfolio"].as<bool>();

    remoteConfig.valid = true;
    configDirty = true;
    Serial.println("[Supa] Config changed via Realtime");
}

// ── Parse incoming pairing event (devices table change or phx_reply) ──
static void parsePairingEvent(JsonObject& payload) {
    // Check if device now has a user_id (pairing happened)
    if (!payload["record"].isNull()) {
        JsonObject record = payload["record"];
        const char* userId = record["user_id"] | (const char*)nullptr;
        if (userId && strlen(userId) > 0) {
            // Device was paired — fetch user tag from the response
            const char* tag = record["lemon_tag"] | "";
            strncpy(lemonTag, tag, sizeof(lemonTag) - 1);

            char uid[48];
            strncpy(uid, userId, sizeof(uid) - 1);
            uid[sizeof(uid) - 1] = '\0';

            nvsSavePairing(uid, lemonTag);
            pairingState = PAIRING_PAIRED;
            Serial.printf("[Supa] Paired with user %s (@%s)\n", uid, lemonTag);
        }
    }
}

// ── WebSocket event handler ──
static void onSupaWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            wsConnected = true;
            reconnectDelay = SUPA_RECONNECT_BASE_MS;
            joinedChannel = false;
            Serial.println("[Supa] WS connected");
            sendPhxJoin();
            break;

        case WStype_DISCONNECTED:
            wsConnected = false;
            joinedChannel = false;
            Serial.println("[Supa] WS disconnected");
            break;

        case WStype_TEXT: {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err) {
                Serial.printf("[Supa] JSON parse error: %s\n", err.c_str());
                break;
            }

            const char* event = doc["event"] | "";

            if (strcmp(event, "phx_reply") == 0) {
                const char* status = doc["payload"]["status"] | "";
                if (strcmp(status, "ok") == 0 && !joinedChannel) {
                    joinedChannel = true;
                    Serial.println("[Supa] Channel joined OK");
                }
            }
            else if (strcmp(event, "postgres_changes") == 0) {
                JsonObject pl = doc["payload"].as<JsonObject>();
                JsonArray data = pl["data"];
                if (!data.isNull()) {
                    for (JsonObject change : data) {
                        const char* table = change["table"] | "";
                        if (strcmp(table, "device_config") == 0) {
                            parseConfigChange(change);
                        } else if (strcmp(table, "devices") == 0) {
                            parsePairingEvent(change);
                        }
                    }
                } else {
                    // Single change object
                    const char* table = pl["table"] | "";
                    if (strcmp(table, "device_config") == 0) {
                        parseConfigChange(pl);
                    } else if (strcmp(table, "devices") == 0) {
                        parsePairingEvent(pl);
                    }
                }
            }
            else if (strcmp(event, "system") == 0) {
                Serial.printf("[Supa] System: %.*s\n", (int)length, payload);
            }
            break;
        }

        case WStype_PING:
        case WStype_PONG:
            break;

        case WStype_ERROR:
            Serial.printf("[Supa] WS error\n");
            break;

        default:
            break;
    }
}

// ── Connect WS to Supabase Realtime ──
static void connectWs() {
    String path = String(SUPABASE_WS_PATH) +
                  "?apikey=" + SUPABASE_ANON_KEY +
                  "&vsn=1.0.0";
    supaWs.beginSSL(SUPABASE_WS_HOST, SUPABASE_WS_PORT, path.c_str());
    supaWs.onEvent(onSupaWsEvent);
    supaWs.setReconnectInterval(0);  // We handle reconnect manually
    Serial.printf("[Supa] Connecting to %s:%d%s\n", SUPABASE_WS_HOST, SUPABASE_WS_PORT, path.c_str());
}

// ══════════════════════════════════════════
//  PUBLIC API
// ══════════════════════════════════════════

void supabaseInit() {
    // Load persisted pairing state
    if (nvsHasDeviceId()) {
        nvsLoadDeviceId(deviceId, sizeof(deviceId));
        nvsLoadPairingCode(pairingCode, sizeof(pairingCode));
        Serial.printf("[Supa] Loaded device ID: %s, code: %s\n", deviceId, pairingCode);

        if (nvsHasPairing()) {
            char uid[48], tag[32];
            nvsLoadPairing(uid, sizeof(uid), tag, sizeof(tag));
            strncpy(lemonTag, tag, sizeof(lemonTag) - 1);
            pairingState = PAIRING_PAIRED;
            Serial.printf("[Supa] Loaded pairing: @%s\n", lemonTag);
        } else {
            pairingState = PAIRING_REGISTERED;
        }
    }

    // Connect Realtime WS if we have a device ID
    if (deviceId[0] != '\0') {
        connectWs();
    }
}

void supabaseLoop() {
    supaWs.loop();

    unsigned long now = millis();

    // Phoenix heartbeat
    if (wsConnected && (now - lastPhxHeartbeat >= SUPA_HEARTBEAT_MS)) {
        lastPhxHeartbeat = now;
        sendPhxHeartbeat();
    }

    // Reconnect with exponential backoff
    if (!wsConnected && deviceId[0] != '\0' && (now - lastReconnectAttempt >= reconnectDelay)) {
        lastReconnectAttempt = now;
        reconnectDelay = min(reconnectDelay * 2, (uint32_t)SUPA_RECONNECT_MAX_MS);
        Serial.printf("[Supa] Reconnecting (next in %lums)...\n", reconnectDelay);
        connectWs();
    }
}

bool supabaseConnected() {
    return wsConnected && joinedChannel;
}

PairingState supabaseGetPairingState() {
    return pairingState;
}

const RemoteConfig& supabaseGetConfig() {
    return remoteConfig;
}

bool supabaseConfigChanged() {
    if (configDirty) {
        configDirty = false;
        return true;
    }
    return false;
}

const char* supabaseGetPairingCode() {
    return pairingCode;
}

const char* supabaseGetLemonTag() {
    return lemonTag;
}

bool supabaseRegister() {
    if (nvsHasDeviceId()) {
        nvsLoadDeviceId(deviceId, sizeof(deviceId));
        nvsLoadPairingCode(pairingCode, sizeof(pairingCode));
        pairingState = nvsHasPairing() ? PAIRING_PAIRED : PAIRING_REGISTERED;
        Serial.printf("[Supa] Already registered: %s (code: %s)\n", deviceId, pairingCode);
        return true;
    }

    Serial.println("[Supa] Registering device...");

    HTTPClient http;
    String url = String(SUPABASE_FUNCTIONS_EP) + "/register-device";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
    http.addHeader("apikey", SUPABASE_ANON_KEY);
    http.setTimeout(SUPA_REGISTER_TIMEOUT_MS);

    String hwId = getHardwareId();
    JsonDocument body;
    body["hardware_id"] = hwId;
    body["firmware_ver"] = APP_VERSION;

    String bodyStr;
    serializeJson(body, bodyStr);

    int code = http.POST(bodyStr);
    if (code == 200 || code == 201) {
        String resp = http.getString();
        JsonDocument respDoc;
        DeserializationError err = deserializeJson(respDoc, resp);
        if (!err) {
            const char* id = respDoc["device_id"] | (const char*)nullptr;
            const char* pc = respDoc["device_code"] | (const char*)nullptr;
            if (id && pc) {
                strncpy(deviceId, id, sizeof(deviceId) - 1);
                strncpy(pairingCode, pc, sizeof(pairingCode) - 1);
                nvsSaveDeviceId(deviceId);
                nvsSavePairingCode(pairingCode);
                pairingState = PAIRING_REGISTERED;
                Serial.printf("[Supa] Registered: id=%s code=%s\n", deviceId, pairingCode);
                http.end();
                return true;
            }
        }
    }

    Serial.printf("[Supa] Registration failed: HTTP %d\n", code);
    pairingState = PAIRING_ERROR;
    http.end();
    return false;
}

void supabaseUnpair() {
    // Notify backend
    if (deviceId[0] != '\0') {
        HTTPClient http;
        String url = String(SUPABASE_FUNCTIONS_EP) + "/unpair-device";
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
        http.addHeader("apikey", SUPABASE_ANON_KEY);
        http.setTimeout(5000);

        JsonDocument body;
        body["device_id"] = deviceId;
        String bodyStr;
        serializeJson(body, bodyStr);
        http.POST(bodyStr);
        http.end();
    }

    // Clear local state
    nvsForgetPairing();
    lemonTag[0] = '\0';
    remoteConfig = {};
    pairingState = PAIRING_REGISTERED;
    Serial.println("[Supa] Unpaired");
}

void supabaseSendHeartbeat() {
    if (deviceId[0] == '\0') return;

    HTTPClient http;
    String url = String(SUPABASE_FUNCTIONS_EP) + "/device-heartbeat";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
    http.addHeader("apikey", SUPABASE_ANON_KEY);
    http.setTimeout(5000);

    JsonDocument body;
    body["device_id"] = deviceId;
    body["firmware_ver"] = APP_VERSION;

    String bodyStr;
    serializeJson(body, bodyStr);

    int code = http.POST(bodyStr);
    if (code == 200) {
        // Check if response contains pairing info (for detecting pairing while on dashboard)
        String resp = http.getString();
        JsonDocument respDoc;
        if (!deserializeJson(respDoc, resp)) {
            const char* userId = respDoc["user_id"] | (const char*)nullptr;
            const char* tag = respDoc["lemon_tag"] | (const char*)nullptr;
            if (userId && strlen(userId) > 0 && pairingState != PAIRING_PAIRED) {
                strncpy(lemonTag, tag ? tag : "", sizeof(lemonTag) - 1);
                nvsSavePairing(userId, lemonTag);
                pairingState = PAIRING_PAIRED;
                Serial.printf("[Supa] Detected pairing via heartbeat: @%s\n", lemonTag);
            }
        }
    }
    http.end();
}

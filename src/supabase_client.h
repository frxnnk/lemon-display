#pragma once

#include <cstdint>

// ── Pairing state machine ──
enum PairingState : uint8_t {
    PAIRING_IDLE,        // Not registered yet
    PAIRING_REGISTERED,  // Has device_id + code, awaiting pairing
    PAIRING_PAIRED,      // Linked to a user
    PAIRING_ERROR,       // Registration/connection failed
};

// ── Remote config pushed from Mini App via Supabase ──
struct RemoteConfig {
    uint8_t  brightness;
    uint8_t  layoutPreset;
    uint8_t  selectedPair;
    uint8_t  selectedPeriod;
    uint8_t  dollarPeriod;
    uint8_t  chartStyle;
    bool     soundEnabled;
    bool     alertEnabled;
    bool     use24h;
    char     greetingText[64];
    bool     showPortfolio;
    bool     valid;          // true when at least one config has been received
};

// ── Public API ──

// Initialize Supabase client (call after WiFi connected)
void supabaseInit();

// Must be called every loop() iteration when on dashboard
void supabaseLoop();

// Connection status
bool supabaseConnected();

// Current pairing state
PairingState supabaseGetPairingState();

// Get latest remote config
const RemoteConfig& supabaseGetConfig();

// Returns true once after each config update (resets after read)
bool supabaseConfigChanged();

// Get 6-char pairing code (valid after registration)
const char* supabaseGetPairingCode();

// Get paired user's lemon tag
const char* supabaseGetLemonTag();

// Register this device with Supabase (blocking HTTP, call once at boot)
// Returns true if registration succeeded
bool supabaseRegister();

// Unpair device (clears local + notifies backend)
void supabaseUnpair();

// Send heartbeat (called by scheduler)
void supabaseSendHeartbeat();

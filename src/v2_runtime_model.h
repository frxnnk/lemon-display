#pragma once

#include <cstdint>
#include "touch_manager.h"

enum V2Scene : uint8_t {
    V2_HOME,
    V2_MARKET_TAPE,
    V2_CONTEXT,
    V2_NEWS_READER,
    V2_SETTINGS,
    V2_WIFI_RECOVERY,
};

enum V2Freshness : uint8_t {
    V2_LOADING,
    V2_LIVE,
    V2_CACHED,
    V2_STALE,
    V2_OFFLINE,
    V2_ERROR,
    V2_RATE_LIMITED,
};

enum V2FetchStatus : uint8_t {
    V2_FETCH_OK,
    V2_FETCH_NETWORK_ERROR,
    V2_FETCH_PARSE_ERROR,
    V2_FETCH_TIMEOUT,
    V2_FETCH_RATE_LIMITED,
};

static constexpr uint32_t V2_HOME_TIMEOUT_MS = 0;
static constexpr uint32_t V2_TAPE_TIMEOUT_MS = 30000;
static constexpr uint32_t V2_CONTEXT_TIMEOUT_MS = 45000;
static constexpr uint32_t V2_NEWS_READER_TIMEOUT_MS = 90000;
static constexpr uint32_t V2_SETTINGS_TIMEOUT_MS = 90000;

struct V2RuntimeModel {
    V2Scene scene = V2_HOME;
    uint8_t settingsPage = 0;
    uint8_t selectedStock = 0;
    uint8_t selectedPair = 0;
    uint8_t selectedNews = 0;
    uint32_t sceneEnteredMs = 0;
    uint32_t lastInteractionMs = 0;
    bool wifiResetArmed = false;
    uint32_t wifiResetUntilMs = 0;
};

constexpr uint8_t v2NextPair(uint8_t selectedPair, uint8_t pairCount) {
    return pairCount == 0 ? 0 : static_cast<uint8_t>((selectedPair + 1) % pairCount);
}

constexpr uint8_t v2NextNews(uint8_t selectedNews, uint8_t newsCount) {
    return newsCount == 0 ? 0
         : static_cast<uint8_t>((selectedNews + 1) % newsCount);
}

constexpr uint32_t v2SceneTimeout(V2Scene scene) {
    return scene == V2_MARKET_TAPE ? V2_TAPE_TIMEOUT_MS
         : scene == V2_CONTEXT ? V2_CONTEXT_TIMEOUT_MS
         : scene == V2_NEWS_READER ? V2_NEWS_READER_TIMEOUT_MS
         : scene == V2_SETTINGS ? V2_SETTINGS_TIMEOUT_MS
         : V2_HOME_TIMEOUT_MS;
}

constexpr V2Scene v2BackScene(V2Scene scene) {
    return scene == V2_CONTEXT ? V2_MARKET_TAPE : V2_HOME;
}

constexpr int8_t v2SettingsTabAt(int16_t x, int16_t y) {
    return y < 72 || y > 118 || x < 24 || x > 456 ? -1
         : x < 171 ? 0
         : x < 310 ? 1
         : 2;
}

constexpr V2Scene v2NextScene(V2Scene scene, TouchGesture gesture,
                              int16_t x, int16_t y) {
    return scene == V2_HOME && gesture == TOUCH_TAP &&
                x >= 410 && x <= 460 && y >= 16 && y <= 56 ? V2_SETTINGS
          : scene == V2_HOME && gesture == TOUCH_TAP &&
                y >= 228 && y < 300 ? V2_NEWS_READER
          : scene != V2_HOME && gesture == TOUCH_TAP &&
                ((scene == V2_SETTINGS && x <= 200 && y <= 72) ||
                 (scene != V2_SETTINGS && x <= 300 && y <= 118)) ? v2BackScene(scene)
          : scene != V2_HOME && scene != V2_SETTINGS &&
                gesture == TOUCH_SWIPE_RIGHT ? v2BackScene(scene)
          : scene;
}

inline bool v2HandleGesture(V2RuntimeModel& model, TouchGesture gesture,
                            int16_t x, int16_t y, uint32_t nowMs) {
    if (gesture == TOUCH_NONE) return false;
    V2Scene next = v2NextScene(model.scene, gesture, x, y);
    if (model.scene == V2_SETTINGS && next == V2_SETTINGS) {
        if (gesture == TOUCH_SWIPE_LEFT && model.settingsPage < 2) model.settingsPage++;
        if (gesture == TOUCH_SWIPE_RIGHT && model.settingsPage > 0) model.settingsPage--;
    }
    if (model.scene == V2_MARKET_TAPE && next == V2_CONTEXT && y >= 132) {
        uint8_t row = static_cast<uint8_t>((y - 132) / 54);
        model.selectedStock = row > 4 ? 4 : row;
    }
    if (next == V2_NEWS_READER && model.scene != V2_NEWS_READER) {
        model.selectedNews = 0;
    }
    bool changed = next != model.scene;
    model.scene = next;
    if (changed) model.sceneEnteredMs = nowMs;
    model.lastInteractionMs = nowMs;
    return changed;
}

inline bool v2ApplyTimeout(V2RuntimeModel& model, uint32_t nowMs) {
    uint32_t timeoutMs = v2SceneTimeout(model.scene);
    if (timeoutMs == 0 || nowMs - model.lastInteractionMs < timeoutMs) return false;
    model.scene = V2_HOME;
    model.sceneEnteredMs = nowMs;
    model.lastInteractionMs = nowMs;
    model.wifiResetArmed = false;
    return true;
}

constexpr V2Freshness v2Freshness(bool valid, uint32_t lastUpdateMs,
                                  uint32_t nowMs, bool online, bool fetching,
                                  V2FetchStatus status, uint32_t freshMs,
                                  uint32_t staleMs) {
    return !online ? V2_OFFLINE
         : !valid && fetching ? V2_LOADING
         : !valid && status == V2_FETCH_RATE_LIMITED ? V2_RATE_LIMITED
         : !valid ? V2_ERROR
         : lastUpdateMs == 0 ? V2_STALE
         : nowMs - lastUpdateMs <= freshMs ? V2_LIVE
         : nowMs - lastUpdateMs <= staleMs ? V2_CACHED
         : V2_STALE;
}

#define V2_MODEL_NAV_ASSERTS 1
static_assert(v2NextScene(V2_HOME, TOUCH_TAP, 240, 250) == V2_NEWS_READER,
              "Home news tap must open the full news reader");
static_assert(v2NextScene(V2_HOME, TOUCH_TAP, 435, 36) == V2_SETTINGS,
              "The Home settings icon must open Settings");
static_assert(v2NextScene(V2_HOME, TOUCH_LONG_PRESS, 435, 36) == V2_HOME,
              "Settings no longer depends on a hidden long press");
static_assert(v2NextScene(V2_MARKET_TAPE, TOUCH_SWIPE_RIGHT, 200, 200) == V2_HOME,
              "Swipe right must return home");
static_assert(v2NextScene(V2_MARKET_TAPE, TOUCH_TAP, 100, 88) == V2_HOME,
              "Tapping the Market Tape header must return home");
static_assert(v2NextScene(V2_CONTEXT, TOUCH_TAP, 100, 88) == V2_MARKET_TAPE,
              "Tapping the Context header must return to Market Tape");
static_assert(v2NextScene(V2_SETTINGS, TOUCH_TAP, 100, 45) == V2_HOME,
              "Tapping the Settings header must return home");
static_assert(v2NextScene(V2_SETTINGS, TOUCH_SWIPE_RIGHT, 200, 200) == V2_SETTINGS,
              "Settings horizontal swipes stay within shallow pages");
static_assert(v2SettingsTabAt(80, 92) == 0 &&
              v2SettingsTabAt(220, 92) == 1 &&
              v2SettingsTabAt(390, 92) == 2,
              "Settings tabs must be directly tappable");
static_assert(v2NextPair(4, 5) == 0, "Pair cycling must wrap to USD");
static_assert(v2NextNews(2, 3) == 0, "News cycling must wrap");
#define V2_MODEL_DIRECT_TOUCH_ASSERTS 1

#define V2_MODEL_FRESHNESS_ASSERTS 1
static_assert(v2Freshness(false, 0, 10, true, true, V2_FETCH_OK, 10, 20) == V2_LOADING,
              "Invalid in-flight data is loading");
static_assert(v2Freshness(true, 10, 15, true, false, V2_FETCH_OK, 10, 20) == V2_LIVE,
              "Young data is live");
static_assert(v2Freshness(true, 10, 25, true, false, V2_FETCH_OK, 10, 20) == V2_CACHED,
              "Older usable data is cached");
static_assert(v2Freshness(true, 0, 25, true, false, V2_FETCH_OK, 10, 20) == V2_STALE,
              "Restored cache is stale until refreshed");
static_assert(v2Freshness(false, 0, 25, false, false, V2_FETCH_NETWORK_ERROR, 10, 20) == V2_OFFLINE,
              "No data without connectivity is offline");
static_assert(v2Freshness(true, 10, 15, false, false, V2_FETCH_OK, 10, 20) == V2_OFFLINE,
              "Cached values remain visible but connectivity state is offline");
static_assert(v2Freshness(false, 0, 25, true, false, V2_FETCH_RATE_LIMITED, 10, 20) == V2_RATE_LIMITED,
              "Provider throttling is explicit");

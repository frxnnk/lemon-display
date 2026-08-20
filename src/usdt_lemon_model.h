#pragma once

#include <cstdint>
#include "touch_manager.h"

enum UsdtScene : uint8_t {
    USDT_OVERVIEW = 0,
    USDT_NETWORKS,
    USDT_MARKETS,
    USDT_REGIONS,
    USDT_SYSTEM,
    USDT_SCENE_COUNT,
};

enum UsdtFreshness : uint8_t {
    USDT_LOADING = 0,
    USDT_LIVE,
    USDT_CACHED,
    USDT_STALE,
    USDT_OFFLINE,
    USDT_ERROR,
    USDT_RATE_LIMITED,
};

enum UsdtFetchStatus : uint8_t {
    USDT_FETCH_OK = 0,
    USDT_FETCH_NETWORK_ERROR,
    USDT_FETCH_PARSE_ERROR,
    USDT_FETCH_TIMEOUT,
    USDT_FETCH_RATE_LIMITED,
};

struct UsdtRuntimeModel {
    UsdtScene scene = USDT_OVERVIEW;
    uint32_t sceneEnteredMs = 0;
    uint32_t lastInteractionMs = 0;
    bool refreshRequested = false;
    bool otaCheckRequested = false;
    bool wifiResetArmed = false;
    uint32_t wifiResetUntilMs = 0;
};

constexpr int16_t USDT_NAV_Y = 416;
constexpr int16_t USDT_NAV_H = 64;
constexpr int16_t USDT_SYSTEM_ACTION_X = 24;
constexpr int16_t USDT_SYSTEM_ACTION_Y = 338;
constexpr int16_t USDT_SYSTEM_ACTION_W = 432;
constexpr int16_t USDT_SYSTEM_ACTION_H = 50;
constexpr uint32_t USDT_FRESH_MS = 60UL * 1000UL;
constexpr uint32_t USDT_CACHED_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t USDT_STALE_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t USDT_SCENE_TIMEOUT_MS = 90UL * 1000UL;
constexpr uint32_t USDT_VARIATION_ROTATE_MS = 4000UL;
constexpr uint32_t USDT_VARIATIONS_REFRESH_MS = 30UL * 60UL * 1000UL;
constexpr uint32_t USDT_VARIATIONS_RETRY_MS = 60UL * 1000UL;
constexpr uint32_t USDT_AUX_MAX_AGE_MS = 2UL * 60UL * 60UL * 1000UL;

constexpr UsdtFreshness usdtFreshness(bool valid, uint32_t lastUpdateMs,
                                      uint32_t nowMs, bool online,
                                      bool fetching, UsdtFetchStatus status) {
    return !online && valid ? USDT_OFFLINE
         : !online ? USDT_OFFLINE
         : !valid && fetching ? USDT_LOADING
         : !valid && status == USDT_FETCH_RATE_LIMITED ? USDT_RATE_LIMITED
         : !valid ? USDT_ERROR
         : lastUpdateMs == 0 ? USDT_STALE
         : nowMs - lastUpdateMs <= USDT_FRESH_MS ? USDT_LIVE
         : nowMs - lastUpdateMs <= USDT_CACHED_MS ? USDT_CACHED
         : USDT_STALE;
}

constexpr bool usdtFreshnessHasData(UsdtFreshness freshness) {
    return freshness == USDT_LIVE || freshness == USDT_CACHED ||
           freshness == USDT_STALE;
}

constexpr UsdtFreshness usdtPrimaryFreshness(UsdtFreshness lemon,
                                             UsdtFreshness peg) {
    return usdtFreshnessHasData(lemon) ? lemon
         : usdtFreshnessHasData(peg) ? peg
         : lemon;
}

constexpr bool usdtAuxDataUsable(bool valid, uint32_t lastUpdateMs,
                                 uint32_t nowMs) {
    return valid && lastUpdateMs != 0 &&
           nowMs - lastUpdateMs <= USDT_AUX_MAX_AGE_MS;
}

constexpr int8_t usdtBottomTabAt(int16_t x, int16_t y) {
    return y < USDT_NAV_Y || y >= 480 || x < 0 || x >= 480 ? -1
         : x < 96 ? 0
         : x < 192 ? 1
         : x < 288 ? 2
         : x < 384 ? 3
         : 4;
}

constexpr UsdtScene usdtSceneForTab(int8_t tab) {
    return tab == 0 ? USDT_OVERVIEW
         : tab == 1 ? USDT_NETWORKS
         : tab == 2 ? USDT_MARKETS
         : tab == 3 ? USDT_REGIONS
         : tab == 4 ? USDT_SYSTEM
         : USDT_OVERVIEW;
}

constexpr bool usdtSystemActionHit(int16_t x, int16_t y) {
    return x >= USDT_SYSTEM_ACTION_X &&
           x < USDT_SYSTEM_ACTION_X + USDT_SYSTEM_ACTION_W &&
           y >= USDT_SYSTEM_ACTION_Y &&
           y < USDT_SYSTEM_ACTION_Y + USDT_SYSTEM_ACTION_H;
}

constexpr uint8_t usdtVariationIndex(uint32_t nowMs) {
    return static_cast<uint8_t>((nowMs / USDT_VARIATION_ROTATE_MS) % 3UL);
}

inline bool usdtHandleGesture(UsdtRuntimeModel& model, const TouchEvent& event,
                              uint32_t nowMs) {
    if (event.gesture == TOUCH_NONE) return false;
    UsdtScene before = model.scene;
    if (event.gesture == TOUCH_TAP) {
        const int8_t tab = usdtBottomTabAt(event.x, event.y);
        if (tab >= 0) model.scene = usdtSceneForTab(tab);
        if (event.y < 96) model.refreshRequested = true;
        if (model.scene == USDT_SYSTEM && event.y >= 250 && event.y < 330) {
            model.otaCheckRequested = true;
        }
    } else if (event.gesture == TOUCH_SWIPE_LEFT) {
        model.scene = static_cast<UsdtScene>(
            (static_cast<uint8_t>(model.scene) + 1) % USDT_SCENE_COUNT);
    } else if (event.gesture == TOUCH_SWIPE_RIGHT) {
        model.scene = static_cast<UsdtScene>(
            (static_cast<uint8_t>(model.scene) + USDT_SCENE_COUNT - 1) %
            USDT_SCENE_COUNT);
    }
    model.lastInteractionMs = nowMs;
    if (before != model.scene) model.sceneEnteredMs = nowMs;
    return before != model.scene || model.refreshRequested ||
           model.otaCheckRequested;
}

inline bool usdtApplyTimeout(UsdtRuntimeModel& model, uint32_t nowMs) {
    if (model.scene == USDT_OVERVIEW ||
        nowMs - model.lastInteractionMs < USDT_SCENE_TIMEOUT_MS) {
        return false;
    }
    model.scene = USDT_OVERVIEW;
    model.sceneEnteredMs = nowMs;
    model.lastInteractionMs = nowMs;
    return true;
}

#define USDT_MODEL_CONTRACT_ASSERTS 1
static_assert(usdtBottomTabAt(48, 448) == 0, "Overview tab hitbox");
static_assert(usdtBottomTabAt(144, 448) == 1, "Networks tab hitbox");
static_assert(usdtBottomTabAt(240, 448) == 2, "Markets tab hitbox");
static_assert(usdtBottomTabAt(336, 448) == 3, "Regions tab hitbox");
static_assert(usdtBottomTabAt(432, 448) == 4, "System tab hitbox");
static_assert(usdtBottomTabAt(240, 400) == -1, "Content is not navigation");
static_assert(usdtSceneForTab(1) == USDT_NETWORKS, "Second tab is networks");
static_assert(usdtVariationIndex(0) == 0, "First variation window is 1h");
static_assert(usdtVariationIndex(4000) == 1, "Second variation window is 24h");
static_assert(usdtVariationIndex(8000) == 2, "Third variation window is 7d");
static_assert(usdtFreshness(false, 0, 1, true, true, USDT_FETCH_OK) ==
              USDT_LOADING, "In-flight first load");
static_assert(usdtFreshness(true, 100, 110, true, false, USDT_FETCH_OK) ==
              USDT_LIVE, "Fresh provider data");
static_assert(usdtFreshness(true, 100, 100 + USDT_FRESH_MS + 1, true, false,
                            USDT_FETCH_OK) == USDT_CACHED,
              "Young cache remains usable");
static_assert(usdtFreshness(true, 100, 100 + USDT_CACHED_MS + 1, true, false,
                            USDT_FETCH_OK) == USDT_STALE,
              "Old cache is visibly stale");
static_assert(usdtFreshness(true, 100, 110, false, false,
                            USDT_FETCH_NETWORK_ERROR) == USDT_OFFLINE,
              "Connectivity state overrides age");
static_assert(usdtPrimaryFreshness(USDT_LIVE, USDT_ERROR) == USDT_LIVE,
              "Healthy Lemon price hides secondary provider errors");
static_assert(usdtPrimaryFreshness(USDT_ERROR, USDT_LIVE) == USDT_LIVE,
              "PEG can provide a healthy fallback status");
static_assert(usdtAuxDataUsable(true, 100, 110), "Fresh auxiliary data is usable");
static_assert(!usdtAuxDataUsable(true, 100, 100 + USDT_AUX_MAX_AGE_MS + 1),
              "Expired auxiliary data is hidden");

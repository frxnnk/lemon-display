#pragma once

#include <cstdint>
#include "touch_manager.h"

enum UsdtScene : uint8_t {
    USDT_OVERVIEW = 0,
    USDT_PEG,
    USDT_NETWORKS,
    USDT_LEMON,
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
    bool wifiResetArmed = false;
    uint32_t wifiResetUntilMs = 0;
};

constexpr uint32_t USDT_FRESH_MS = 60UL * 1000UL;
constexpr uint32_t USDT_CACHED_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t USDT_STALE_MS = 60UL * 60UL * 1000UL;

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

constexpr int8_t usdtBottomTabAt(int16_t x, int16_t y) {
    return y < 416 || y >= 480 || x < 0 || x >= 480 ? -1
         : x < 96 ? 0
         : x < 192 ? 1
         : x < 288 ? 2
         : x < 384 ? 3
         : 4;
}

constexpr UsdtScene usdtSceneForTab(int8_t tab) {
    return tab == 0 ? USDT_OVERVIEW
         : tab == 1 ? USDT_PEG
         : tab == 2 ? USDT_NETWORKS
         : tab == 3 ? USDT_LEMON
         : tab == 4 ? USDT_SYSTEM
         : USDT_OVERVIEW;
}

inline bool usdtHandleGesture(UsdtRuntimeModel& model, TouchGesture gesture,
                              int16_t x, int16_t y, uint32_t nowMs) {
    if (gesture == TOUCH_NONE) return false;
    UsdtScene before = model.scene;
    if (gesture == TOUCH_TAP) {
        const int8_t tab = usdtBottomTabAt(x, y);
        if (tab >= 0) model.scene = usdtSceneForTab(tab);
        if (y >= 72 && y < 132 && x >= 360 && x < 464) {
            model.refreshRequested = true;
        }
    } else if (gesture == TOUCH_SWIPE_LEFT) {
        model.scene = static_cast<UsdtScene>(
            (static_cast<uint8_t>(model.scene) + 1) % USDT_SCENE_COUNT);
    } else if (gesture == TOUCH_SWIPE_RIGHT) {
        model.scene = static_cast<UsdtScene>(
            (static_cast<uint8_t>(model.scene) + USDT_SCENE_COUNT - 1) %
            USDT_SCENE_COUNT);
    }
    model.lastInteractionMs = nowMs;
    if (before != model.scene) model.sceneEnteredMs = nowMs;
    return before != model.scene;
}

#define USDT_MODEL_CONTRACT_ASSERTS 1
static_assert(usdtBottomTabAt(48, 448) == 0, "Overview tab hitbox");
static_assert(usdtBottomTabAt(144, 448) == 1, "Peg tab hitbox");
static_assert(usdtBottomTabAt(240, 448) == 2, "Networks tab hitbox");
static_assert(usdtBottomTabAt(336, 448) == 3, "Lemon tab hitbox");
static_assert(usdtBottomTabAt(432, 448) == 4, "System tab hitbox");
static_assert(usdtBottomTabAt(240, 400) == -1, "Content is not navigation");
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

#pragma once

#include <stdint.h>


enum V2DemoScene : uint8_t {
    V2_DEMO_HOME,
    V2_DEMO_PREOPEN,
    V2_DEMO_OPEN,
    V2_DEMO_TAPE,
    V2_DEMO_PACK,
    V2_DEMO_NEWS,
    V2_DEMO_CONTEXT,
    V2_DEMO_QR,
    V2_DEMO_RETURN,
};

struct V2DemoSegment {
    V2DemoScene scene;
    uint32_t startMs;
    uint32_t endMs;
};

struct V2DemoFrame {
    V2DemoScene scene;
    uint32_t sceneElapsedMs;
    uint32_t timelineElapsedMs;
};

static constexpr uint32_t V2_DEMO_DURATION_MS = 78000;

static constexpr V2DemoSegment V2_DEMO_TIMELINE[] = {
    { V2_DEMO_HOME,         0,  8000 },
    { V2_DEMO_PREOPEN,   8000, 15000 },
    { V2_DEMO_OPEN,     15000, 19000 },
    { V2_DEMO_TAPE,     19000, 34000 },
    { V2_DEMO_PACK,     34000, 43000 },
    { V2_DEMO_NEWS,     43000, 58000 },
    { V2_DEMO_CONTEXT,  58000, 67000 },
    { V2_DEMO_QR,       67000, 74000 },
    { V2_DEMO_RETURN,   74000, 78000 },
};

static constexpr uint8_t V2_DEMO_SCENE_COUNT =
    sizeof(V2_DEMO_TIMELINE) / sizeof(V2_DEMO_TIMELINE[0]);

inline uint32_t v2DemoSceneStartMs(V2DemoScene scene) {
    for (uint8_t i = 0; i < V2_DEMO_SCENE_COUNT; ++i) {
        if (V2_DEMO_TIMELINE[i].scene == scene) {
            return V2_DEMO_TIMELINE[i].startMs;
        }
    }
    return 0;
}

inline V2DemoFrame v2DemoFrameAt(uint32_t elapsedMs) {
    const uint32_t wrapped = elapsedMs % V2_DEMO_DURATION_MS;
    for (uint8_t i = 0; i < V2_DEMO_SCENE_COUNT; ++i) {
        const V2DemoSegment& segment = V2_DEMO_TIMELINE[i];
        if (wrapped < segment.endMs) {
            return { segment.scene, wrapped - segment.startMs, wrapped };
        }
    }
    return { V2_DEMO_HOME, 0, 0 };
}

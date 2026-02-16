#pragma once

#include <Arduino.h>
#include <functional>

#define MAX_SCHEDULED_TASKS 8

struct ScheduledTask {
    const char* name;
    std::function<void()> callback;
    unsigned long intervalMs;
    unsigned long lastRun;
    bool enabled;
};

class Scheduler {
    ScheduledTask tasks[MAX_SCHEDULED_TASKS];
    uint8_t count = 0;

public:
    // Add a task. Returns task index (0-based).
    uint8_t add(const char* name, unsigned long intervalMs, std::function<void()> cb);

    // Check all tasks, run if due
    void tick();

    // Manual trigger by index (ignores interval, resets timer)
    void forceRun(uint8_t id);

    // Enable/disable a task
    void enable(uint8_t id, bool on);
};

#include "scheduler.h"

uint8_t Scheduler::add(const char* name, unsigned long intervalMs, std::function<void()> cb) {
    if (count >= MAX_SCHEDULED_TASKS) {
        Serial.printf("[Scheduler] Max tasks reached, cannot add '%s'\n", name);
        return 255;
    }
    tasks[count].name = name;
    tasks[count].callback = cb;
    tasks[count].intervalMs = intervalMs;
    tasks[count].lastRun = 0;
    tasks[count].enabled = true;
    return count++;
}

void Scheduler::tick() {
    unsigned long now = millis();
    for (uint8_t i = 0; i < count; i++) {
        if (!tasks[i].enabled) continue;
        if (now - tasks[i].lastRun >= tasks[i].intervalMs) {
            tasks[i].lastRun = now;
            if (tasks[i].callback) {
                tasks[i].callback();
            }
        }
    }
}

void Scheduler::forceRun(uint8_t id) {
    if (id >= count) return;
    tasks[id].lastRun = millis();
    if (tasks[id].callback) {
        Serial.printf("[Scheduler] Force run: %s\n", tasks[id].name);
        tasks[id].callback();
    }
}

void Scheduler::enable(uint8_t id, bool on) {
    if (id >= count) return;
    tasks[id].enabled = on;
}

#include "todo_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <cstring>

namespace {

constexpr const char* TODO_FILE = "/todo-v2.bin";
constexpr const char* TODO_FILE_TMP = "/todo-v2.bin.tmp";
constexpr const char* TODO_FILE_BAK = "/todo-v2.bin.bak";
constexpr uint32_t FILE_MAGIC = 0x324F4454UL;  // TDO2
constexpr const char* NS_LEGACY = "ferced_todo";

struct FileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t payloadSize;
    uint32_t checksum;
};

struct LegacyTodoItem {
    char text[64];
    bool done;
};

TodoState state;
uint32_t revision = 0;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t saveMutex = nullptr;

uint32_t checksum(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

bool readFile(const char* path, TodoState& output) {
    File file = LittleFS.open(path, "r");
    if (!file) return false;
    FileHeader header{};
    const bool headerOk = file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header);
    const bool shapeOk = headerOk && header.magic == FILE_MAGIC &&
                         header.version == TODO_MODEL_VERSION &&
                         header.payloadSize == sizeof(TodoState);
    const bool payloadOk = shapeOk &&
        file.read(reinterpret_cast<uint8_t*>(&output), sizeof(output)) == sizeof(output);
    file.close();
    if (!payloadOk || output.version != TODO_MODEL_VERSION || output.count > TODO_MAX_ITEMS) return false;
    return checksum(&output, sizeof(output)) == header.checksum;
}

bool writeFile(const char* path, const TodoState& input) {
    File file = LittleFS.open(path, "w");
    if (!file) return false;
    const FileHeader header{FILE_MAGIC, TODO_MODEL_VERSION, 0,
                            static_cast<uint32_t>(sizeof(TodoState)),
                            checksum(&input, sizeof(input))};
    const bool ok = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
                    file.write(reinterpret_cast<const uint8_t*>(&input), sizeof(input)) == sizeof(input);
    file.flush();
    file.close();
    return ok;
}

bool persist() {
    if (!saveMutex) saveMutex = xSemaphoreCreateMutex();
    if (!saveMutex || xSemaphoreTake(saveMutex, pdMS_TO_TICKS(2000)) != pdTRUE) return false;

    TodoState* snapshot = new TodoState;
    if (!snapshot) {
        xSemaphoreGive(saveMutex);
        return false;
    }
    portENTER_CRITICAL(&stateMux);
    std::memcpy(snapshot, &state, sizeof(state));
    portEXIT_CRITICAL(&stateMux);

    LittleFS.remove(TODO_FILE_TMP);
    bool ok = writeFile(TODO_FILE_TMP, *snapshot);
    TodoState verify{};
    ok = ok && readFile(TODO_FILE_TMP, verify);
    if (ok) {
        LittleFS.remove(TODO_FILE_BAK);
        if (LittleFS.exists(TODO_FILE)) ok = LittleFS.rename(TODO_FILE, TODO_FILE_BAK);
        if (ok) ok = LittleFS.rename(TODO_FILE_TMP, TODO_FILE);
        if (!ok && LittleFS.exists(TODO_FILE_BAK) && !LittleFS.exists(TODO_FILE)) {
            LittleFS.rename(TODO_FILE_BAK, TODO_FILE);
        }
        if (ok) LittleFS.remove(TODO_FILE_BAK);
    }
    delete snapshot;
    xSemaphoreGive(saveMutex);
    if (!ok) Serial.println("[Todo] no pude guardar el archivo v2");
    return ok;
}

bool migrateLegacy() {
    Preferences prefs;
    if (!prefs.begin(NS_LEGACY, true)) return false;
    const uint8_t version = prefs.getUChar("ver", 0);
    const uint8_t count = prefs.getUChar("cnt", 0);
    LegacyTodoItem legacy[16]{};
    const size_t expected = sizeof(legacy);
    const bool available = version == 1 && prefs.getBytesLength("items") == expected;
    if (available) prefs.getBytes("items", legacy, expected);
    prefs.end();
    if (!available) return false;

    for (uint8_t i = 0; i < count && i < 16; ++i) {
        legacy[i].text[sizeof(legacy[i].text) - 1] = '\0';
        const uint32_t id = todoModelAddTask(state, legacy[i].text);
        if (id && legacy[i].done) todoModelSetTaskDone(state, id, true);
    }
    if (!persist()) return false;
    Serial.printf("[Todo] migré %u tareas de NVS al formato v2\n", state.count);
    return true;
}

template <typename Mutation>
bool mutate(Mutation operation) {
    portENTER_CRITICAL(&stateMux);
    const bool changed = operation(state);
    if (changed) ++revision;
    portEXIT_CRITICAL(&stateMux);
    if (changed) persist();
    return changed;
}

}  // namespace

void todoLoad() {
    todoModelInit(state);
    if (!LittleFS.begin(true)) {
        Serial.println("[Todo] LittleFS no montó");
        ++revision;
        return;
    }
    TodoState loaded{};
    if (readFile(TODO_FILE, loaded) || readFile(TODO_FILE_BAK, loaded)) {
        state = loaded;
    } else {
        migrateLegacy();
    }
    ++revision;
    Serial.printf("[Todo] %u tareas, %u pendientes (v2)\n", state.count, todoPending());
}

uint32_t todoRevision() { return revision; }
uint8_t todoCount() { return state.count; }

uint8_t todoPending() {
    uint8_t result = 0;
    for (uint8_t i = 0; i < state.count; ++i) if (!state.items[i].done) ++result;
    return result;
}

const TodoItem* todoItem(uint8_t index) {
    return index < state.count ? &state.items[index] : nullptr;
}

const TodoItem* todoGetById(uint32_t id) { return todoModelFind(state, id); }

uint32_t todoAddFull(const char* title, const char* description,
                     uint32_t dueEpoch, uint32_t reminderEpoch) {
    uint32_t id = 0;
    portENTER_CRITICAL(&stateMux);
    id = todoModelAddTask(state, title);
    if (id) {
        todoModelUpdateTask(state, id, title, description, dueEpoch, reminderEpoch);
        ++revision;
    }
    portEXIT_CRITICAL(&stateMux);
    if (id) persist();
    return id;
}

bool todoUpdate(uint32_t id, const char* title, const char* description,
                uint32_t dueEpoch, uint32_t reminderEpoch) {
    return mutate([&](TodoState& s) { return todoModelUpdateTask(s, id, title, description, dueEpoch, reminderEpoch); });
}

bool todoRemoveById(uint32_t id) {
    return mutate([&](TodoState& s) { return todoModelRemoveTask(s, id); });
}

bool todoMove(uint32_t id, uint8_t position) {
    return mutate([&](TodoState& s) { return todoModelMoveTask(s, id, position); });
}

bool todoSetDone(uint32_t id, bool done) {
    return mutate([&](TodoState& s) { return todoModelSetTaskDone(s, id, done); });
}

bool todoSetCollapsed(uint32_t id, bool collapsed) {
    return mutate([&](TodoState& s) { return todoModelSetCollapsed(s, id, collapsed); });
}

uint32_t todoAddSubtask(uint32_t taskId, const char* title) {
    uint32_t id = 0;
    portENTER_CRITICAL(&stateMux);
    id = todoModelAddSubtask(state, taskId, title);
    if (id) ++revision;
    portEXIT_CRITICAL(&stateMux);
    if (id) persist();
    return id;
}

bool todoUpdateSubtask(uint32_t taskId, uint32_t subtaskId, const char* title) {
    return mutate([&](TodoState& s) { return todoModelUpdateSubtask(s, taskId, subtaskId, title); });
}

bool todoRemoveSubtask(uint32_t taskId, uint32_t subtaskId) {
    return mutate([&](TodoState& s) { return todoModelRemoveSubtask(s, taskId, subtaskId); });
}

bool todoMoveSubtask(uint32_t taskId, uint32_t subtaskId, uint8_t position) {
    return mutate([&](TodoState& s) { return todoModelMoveSubtask(s, taskId, subtaskId, position); });
}

bool todoSetSubtaskDone(uint32_t taskId, uint32_t subtaskId, bool done) {
    return mutate([&](TodoState& s) { return todoModelSetSubtaskDone(s, taskId, subtaskId, done); });
}

bool todoMarkReminderFired(uint32_t taskId) {
    return mutate([&](TodoState& s) { return todoModelMarkReminderFired(s, taskId); });
}

bool todoSnooze(uint32_t taskId, uint32_t reminderEpoch) {
    return mutate([&](TodoState& s) { return todoModelSnooze(s, taskId, reminderEpoch); });
}

bool todoAdd(const char* title) { return todoAddFull(title) != 0; }

bool todoToggle(uint8_t index) {
    const TodoItem* item = todoItem(index);
    return item ? todoSetDone(item->id, !item->done) : false;
}

bool todoRemove(uint8_t index) {
    const TodoItem* item = todoItem(index);
    return item ? todoRemoveById(item->id) : false;
}

bool todoClearDone() {
    bool any = false;
    for (int i = static_cast<int>(todoCount()) - 1; i >= 0; --i) {
        const TodoItem* item = todoItem(static_cast<uint8_t>(i));
        if (item && item->done) {
            any = todoRemoveById(item->id) || any;
        }
    }
    return any;
}

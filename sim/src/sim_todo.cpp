// Store de tareas para el simulador: usa el mismo modelo que el firmware y un
// fixture de texto en lugar de LittleFS.

#include "todo_store.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
TodoState state;
uint32_t revision = 0;

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t found = text.find(separator, start);
        parts.push_back(text.substr(start, found == std::string::npos ? found : found - start));
        if (found == std::string::npos) break;
        start = found + 1;
    }
    return parts;
}

bool changed(bool ok) { if (ok) ++revision; return ok; }
}  // namespace

uint32_t todoRevision() { return revision; }
uint8_t todoCount() { return state.count; }
uint8_t todoPending() {
    uint8_t result = 0;
    for (uint8_t i = 0; i < state.count; ++i) if (!state.items[i].done) ++result;
    return result;
}
const TodoItem* todoItem(uint8_t index) { return index < state.count ? &state.items[index] : nullptr; }
const TodoItem* todoGetById(uint32_t id) { return todoModelFind(state, id); }
const TodoItem* todoFindDueReminder(uint32_t nowEpoch) { return todoModelFindDueReminder(state, nowEpoch); }

void todoLoad() {
    todoModelInit(state);
    const char* path = std::getenv("SIM_TODO");
    const std::string filename = path ? path : "data/todo.txt";
    FILE* file = std::fopen(filename.c_str(), "r");
    if (!file) {
        std::printf("[sim] sin fixture de tareas (%s)\n", filename.c_str());
        ++revision;
        return;
    }
    char line[1024];
    while (std::fgets(line, sizeof(line), file) && state.count < TODO_MAX_ITEMS) {
        std::string row(line);
        while (!row.empty() && (row.back() == '\n' || row.back() == '\r')) row.pop_back();
        if (row.empty() || row[0] == '#') continue;
        bool done = row.size() > 2 && (row[0] == 'x' || row[0] == 'X') && row[1] == ' ';
        if (done) row.erase(0, 2);
        const auto fields = split(row, '|');
        const uint32_t id = todoModelAddTask(state, fields[0].c_str());
        if (!id) continue;
        todoModelUpdateTask(state, id, fields[0].c_str(),
                            fields.size() > 1 ? fields[1].c_str() : "",
                            fields.size() > 2 ? static_cast<uint32_t>(std::strtoul(fields[2].c_str(), nullptr, 10)) : 0,
                            fields.size() > 3 ? static_cast<uint32_t>(std::strtoul(fields[3].c_str(), nullptr, 10)) : 0);
        if (fields.size() > 4) {
            for (const auto& sub : split(fields[4], ';')) if (!sub.empty()) todoModelAddSubtask(state, id, sub.c_str());
        }
        if (done) todoModelSetTaskDone(state, id, true);
    }
    std::fclose(file);
    ++revision;
    std::printf("[sim] tareas v2: %u, %u pendientes\n", state.count, todoPending());
}

uint32_t todoAddFull(const char* title, const char* description, uint32_t due, uint32_t reminder) {
    const uint32_t id = todoModelAddTask(state, title);
    if (id) { todoModelUpdateTask(state, id, title, description, due, reminder); ++revision; }
    return id;
}
bool todoUpdate(uint32_t id, const char* title, const char* description, uint32_t due, uint32_t reminder) { return changed(todoModelUpdateTask(state, id, title, description, due, reminder)); }
bool todoRemoveById(uint32_t id) { return changed(todoModelRemoveTask(state, id)); }
bool todoMove(uint32_t id, uint8_t position) { return changed(todoModelMoveTask(state, id, position)); }
bool todoSetDone(uint32_t id, bool done) { return changed(todoModelSetTaskDone(state, id, done)); }
bool todoSetCollapsed(uint32_t id, bool collapsed) { return changed(todoModelSetCollapsed(state, id, collapsed)); }
uint32_t todoAddSubtask(uint32_t taskId, const char* title) { const uint32_t id = todoModelAddSubtask(state, taskId, title); if (id) ++revision; return id; }
bool todoUpdateSubtask(uint32_t taskId, uint32_t subId, const char* title) { return changed(todoModelUpdateSubtask(state, taskId, subId, title)); }
bool todoRemoveSubtask(uint32_t taskId, uint32_t subId) { return changed(todoModelRemoveSubtask(state, taskId, subId)); }
bool todoMoveSubtask(uint32_t taskId, uint32_t subId, uint8_t position) { return changed(todoModelMoveSubtask(state, taskId, subId, position)); }
bool todoSetSubtaskDone(uint32_t taskId, uint32_t subId, bool done) { return changed(todoModelSetSubtaskDone(state, taskId, subId, done)); }
bool todoMarkReminderFired(uint32_t taskId) { return changed(todoModelMarkReminderFired(state, taskId)); }
bool todoSnooze(uint32_t taskId, uint32_t epoch) { return changed(todoModelSnooze(state, taskId, epoch)); }
bool todoAdd(const char* title) { return todoAddFull(title) != 0; }
bool todoToggle(uint8_t index) { const TodoItem* item = todoItem(index); return item && todoSetDone(item->id, !item->done); }
bool todoRemove(uint8_t index) { const TodoItem* item = todoItem(index); return item && todoRemoveById(item->id); }
bool todoClearDone() {
    bool any = false;
    for (int i = state.count - 1; i >= 0; --i) if (state.items[i].done) any = todoModelRemoveTask(state, state.items[i].id) || any;
    if (any) ++revision;
    return any;
}

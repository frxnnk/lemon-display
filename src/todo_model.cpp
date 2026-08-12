#include "todo_model.h"

#include <cstring>

namespace {

bool cleanSingleLine(const char* input, char* output, size_t capacity) {
    if (!input || !output || capacity == 0) return false;
    size_t written = 0;
    bool pendingSpace = false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(input);
         *p && written + 1 < capacity; ++p) {
        unsigned char c = *p;
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if (c < 0x20) continue;
        if (c == ' ') {
            if (written) pendingSpace = true;
            continue;
        }
        if (pendingSpace && written + 1 < capacity) output[written++] = ' ';
        pendingSpace = false;
        if (written + 1 < capacity) output[written++] = static_cast<char>(c);
    }
    output[written] = '\0';
    return written != 0;
}

void cleanDescription(const char* input, char* output, size_t capacity) {
    if (!output || capacity == 0) return;
    if (!input) {
        output[0] = '\0';
        return;
    }
    size_t written = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(input);
         *p && written + 1 < capacity; ++p) {
        unsigned char c = *p;
        if (c == '\r') continue;
        if (c == '\t') c = ' ';
        if (c < 0x20 && c != '\n') continue;
        output[written++] = static_cast<char>(c);
    }
    while (written && (output[written - 1] == ' ' || output[written - 1] == '\n')) --written;
    output[written] = '\0';
}

uint32_t allocateId(TodoState& state) {
    if (state.nextId == 0) state.nextId = 1;
    return state.nextId++;
}

int taskIndex(const TodoState& state, uint32_t id) {
    for (uint8_t i = 0; i < state.count; ++i) {
        if (state.items[i].id == id) return i;
    }
    return -1;
}

int subtaskIndex(const TodoItem& task, uint32_t id) {
    for (uint8_t i = 0; i < task.subCount; ++i) {
        if (task.subtasks[i].id == id) return i;
    }
    return -1;
}

void deriveParentDone(TodoItem& task) {
    if (task.subCount == 0) return;
    for (uint8_t i = 0; i < task.subCount; ++i) {
        if (!task.subtasks[i].done) {
            task.done = false;
            return;
        }
    }
    task.done = true;
}

}  // namespace

void todoModelInit(TodoState& state) {
    std::memset(&state, 0, sizeof(state));
    state.version = TODO_MODEL_VERSION;
    state.nextId = 1;
}

TodoItem* todoModelFind(TodoState& state, uint32_t id) {
    const int index = taskIndex(state, id);
    return index < 0 ? nullptr : &state.items[index];
}

const TodoItem* todoModelFind(const TodoState& state, uint32_t id) {
    const int index = taskIndex(state, id);
    return index < 0 ? nullptr : &state.items[index];
}

uint32_t todoModelAddTask(TodoState& state, const char* title) {
    if (state.count >= TODO_MAX_ITEMS) return 0;
    TodoItem item{};
    if (!cleanSingleLine(title, item.title, sizeof(item.title))) return 0;
    item.id = allocateId(state);
    state.items[state.count++] = item;
    return item.id;
}

bool todoModelUpdateTask(TodoState& state, uint32_t id, const char* title,
                         const char* description, uint32_t dueEpoch,
                         uint32_t reminderEpoch) {
    TodoItem* item = todoModelFind(state, id);
    if (!item) return false;
    char cleanTitle[TODO_TITLE_LEN];
    if (!cleanSingleLine(title, cleanTitle, sizeof(cleanTitle))) return false;
    std::memcpy(item->title, cleanTitle, sizeof(item->title));
    cleanDescription(description, item->description, sizeof(item->description));
    item->dueEpoch = dueEpoch;
    if (item->reminderEpoch != reminderEpoch) item->reminderFired = false;
    item->reminderEpoch = reminderEpoch;
    return true;
}

bool todoModelRemoveTask(TodoState& state, uint32_t id) {
    const int index = taskIndex(state, id);
    if (index < 0) return false;
    for (uint8_t i = static_cast<uint8_t>(index); i + 1 < state.count; ++i) {
        state.items[i] = state.items[i + 1];
    }
    --state.count;
    std::memset(&state.items[state.count], 0, sizeof(TodoItem));
    return true;
}

bool todoModelMoveTask(TodoState& state, uint32_t id, uint8_t position) {
    const int from = taskIndex(state, id);
    if (from < 0 || state.count == 0) return false;
    if (position >= state.count) position = state.count - 1;
    if (from == position) return true;
    TodoItem moving = state.items[from];
    if (from < position) {
        for (int i = from; i < position; ++i) state.items[i] = state.items[i + 1];
    } else {
        for (int i = from; i > position; --i) state.items[i] = state.items[i - 1];
    }
    state.items[position] = moving;
    return true;
}

bool todoModelSetTaskDone(TodoState& state, uint32_t id, bool done) {
    TodoItem* item = todoModelFind(state, id);
    if (!item) return false;
    item->done = done;
    for (uint8_t i = 0; i < item->subCount; ++i) item->subtasks[i].done = done;
    return true;
}

bool todoModelSetCollapsed(TodoState& state, uint32_t id, bool collapsed) {
    TodoItem* item = todoModelFind(state, id);
    if (!item) return false;
    item->collapsed = collapsed;
    return true;
}

uint32_t todoModelAddSubtask(TodoState& state, uint32_t taskId, const char* title) {
    TodoItem* item = todoModelFind(state, taskId);
    if (!item || item->subCount >= TODO_MAX_SUBTASKS) return 0;
    TodoSubtask sub{};
    if (!cleanSingleLine(title, sub.title, sizeof(sub.title))) return 0;
    sub.id = allocateId(state);
    item->subtasks[item->subCount++] = sub;
    if (item->done) item->done = false;
    return sub.id;
}

bool todoModelUpdateSubtask(TodoState& state, uint32_t taskId, uint32_t subtaskId,
                            const char* title) {
    TodoItem* item = todoModelFind(state, taskId);
    if (!item) return false;
    const int index = subtaskIndex(*item, subtaskId);
    if (index < 0) return false;
    char cleanTitle[TODO_SUBTASK_TITLE_LEN];
    if (!cleanSingleLine(title, cleanTitle, sizeof(cleanTitle))) return false;
    std::memcpy(item->subtasks[index].title, cleanTitle, sizeof(item->subtasks[index].title));
    return true;
}

bool todoModelRemoveSubtask(TodoState& state, uint32_t taskId, uint32_t subtaskId) {
    TodoItem* item = todoModelFind(state, taskId);
    if (!item) return false;
    const int index = subtaskIndex(*item, subtaskId);
    if (index < 0) return false;
    for (uint8_t i = static_cast<uint8_t>(index); i + 1 < item->subCount; ++i) {
        item->subtasks[i] = item->subtasks[i + 1];
    }
    --item->subCount;
    std::memset(&item->subtasks[item->subCount], 0, sizeof(TodoSubtask));
    deriveParentDone(*item);
    return true;
}

bool todoModelMoveSubtask(TodoState& state, uint32_t taskId, uint32_t subtaskId,
                          uint8_t position) {
    TodoItem* item = todoModelFind(state, taskId);
    if (!item || item->subCount == 0) return false;
    const int from = subtaskIndex(*item, subtaskId);
    if (from < 0) return false;
    if (position >= item->subCount) position = item->subCount - 1;
    if (from == position) return true;
    TodoSubtask moving = item->subtasks[from];
    if (from < position) {
        for (int i = from; i < position; ++i) item->subtasks[i] = item->subtasks[i + 1];
    } else {
        for (int i = from; i > position; --i) item->subtasks[i] = item->subtasks[i - 1];
    }
    item->subtasks[position] = moving;
    return true;
}

bool todoModelSetSubtaskDone(TodoState& state, uint32_t taskId, uint32_t subtaskId,
                             bool done) {
    TodoItem* item = todoModelFind(state, taskId);
    if (!item) return false;
    const int index = subtaskIndex(*item, subtaskId);
    if (index < 0) return false;
    item->subtasks[index].done = done;
    deriveParentDone(*item);
    return true;
}

bool todoModelMarkReminderFired(TodoState& state, uint32_t taskId) {
    TodoItem* item = todoModelFind(state, taskId);
    if (!item || item->reminderEpoch == 0) return false;
    item->reminderFired = true;
    return true;
}

bool todoModelSnooze(TodoState& state, uint32_t taskId, uint32_t reminderEpoch) {
    TodoItem* item = todoModelFind(state, taskId);
    if (!item || reminderEpoch == 0) return false;
    item->reminderEpoch = reminderEpoch;
    item->reminderFired = false;
    return true;
}

const TodoItem* todoModelFindDueReminder(const TodoState& state, uint32_t nowEpoch) {
    if (nowEpoch == 0) return nullptr;
    for (uint8_t i = 0; i < state.count; ++i) {
        const TodoItem& item = state.items[i];
        if (!item.done && item.reminderEpoch != 0 &&
            !item.reminderFired && item.reminderEpoch <= nowEpoch) {
            return &item;
        }
    }
    return nullptr;
}

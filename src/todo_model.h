#pragma once

#include <cstddef>
#include <cstdint>

constexpr uint8_t TODO_MAX_ITEMS = 24;
constexpr uint8_t TODO_MAX_SUBTASKS = 8;
constexpr size_t TODO_TITLE_LEN = 128;
constexpr size_t TODO_DESCRIPTION_LEN = 384;
constexpr size_t TODO_SUBTASK_TITLE_LEN = 96;
constexpr uint16_t TODO_MODEL_VERSION = 2;

struct TodoSubtask {
    uint32_t id;
    char title[TODO_SUBTASK_TITLE_LEN];
    bool done;
};

struct TodoItem {
    uint32_t id;
    char title[TODO_TITLE_LEN];
    char description[TODO_DESCRIPTION_LEN];
    uint32_t dueEpoch;
    uint32_t reminderEpoch;
    bool done;
    bool collapsed;
    bool reminderFired;
    uint8_t subCount;
    TodoSubtask subtasks[TODO_MAX_SUBTASKS];
};

struct TodoState {
    uint16_t version;
    uint8_t count;
    uint32_t nextId;
    TodoItem items[TODO_MAX_ITEMS];
};

void todoModelInit(TodoState& state);
TodoItem* todoModelFind(TodoState& state, uint32_t id);
const TodoItem* todoModelFind(const TodoState& state, uint32_t id);

uint32_t todoModelAddTask(TodoState& state, const char* title);
bool todoModelUpdateTask(TodoState& state, uint32_t id, const char* title,
                         const char* description, uint32_t dueEpoch,
                         uint32_t reminderEpoch);
bool todoModelRemoveTask(TodoState& state, uint32_t id);
bool todoModelMoveTask(TodoState& state, uint32_t id, uint8_t position);
bool todoModelSetTaskDone(TodoState& state, uint32_t id, bool done);
bool todoModelSetCollapsed(TodoState& state, uint32_t id, bool collapsed);

uint32_t todoModelAddSubtask(TodoState& state, uint32_t taskId, const char* title);
bool todoModelUpdateSubtask(TodoState& state, uint32_t taskId, uint32_t subtaskId,
                            const char* title);
bool todoModelRemoveSubtask(TodoState& state, uint32_t taskId, uint32_t subtaskId);
bool todoModelMoveSubtask(TodoState& state, uint32_t taskId, uint32_t subtaskId,
                          uint8_t position);
bool todoModelSetSubtaskDone(TodoState& state, uint32_t taskId, uint32_t subtaskId,
                             bool done);

bool todoModelMarkReminderFired(TodoState& state, uint32_t taskId);
bool todoModelSnooze(TodoState& state, uint32_t taskId, uint32_t reminderEpoch);
const TodoItem* todoModelFindDueReminder(const TodoState& state, uint32_t nowEpoch);

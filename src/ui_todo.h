#pragma once

#include <stdint.h>

#include "todo_store.h"
#include "ui_anim.h"

enum TodoUiActionType : uint8_t {
    TODO_UI_NONE,
    TODO_UI_TOGGLE_TASK,
    TODO_UI_TOGGLE_SUBTASK,
    TODO_UI_OPEN_DETAIL,
    TODO_UI_TOGGLE_COLLAPSE,
    TODO_UI_BACK,
};

struct TodoUiAction {
    TodoUiActionType type;
    uint32_t taskId;
    uint32_t subtaskId;
};

enum TodoReminderAction : uint8_t {
    TODO_REMINDER_NONE,
    TODO_REMINDER_COMPLETE,
    TODO_REMINDER_SNOOZE,
    TODO_REMINDER_OPEN,
};

void uiTodoSetup();
void uiTodoDraw(const char* direccion);
TodoUiAction uiTodoTap(int16_t x, int16_t y);
void uiTodoScroll(int16_t delta);
void uiTodoOpen(uint32_t taskId);
void uiTodoBack();
bool uiTodoIsDetail();
uint8_t uiTodoVisibles();
void uiTodoDrawReminder(const TodoItem* item);
TodoReminderAction uiTodoReminderTap(int16_t x, int16_t y);

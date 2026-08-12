#pragma once

#include "todo_model.h"

#include <cstdint>

// Estado privado del aparato. La API pública trabaja por ID para que un refresh
// o un reordenamiento no convierta un índice viejo en otra tarea.
void todoLoad();
uint32_t todoRevision();
uint8_t todoCount();
uint8_t todoPending();
const TodoItem* todoItem(uint8_t index);
const TodoItem* todoGetById(uint32_t id);
const TodoItem* todoFindDueReminder(uint32_t nowEpoch);

uint32_t todoAddFull(const char* title, const char* description = "",
                     uint32_t dueEpoch = 0, uint32_t reminderEpoch = 0);
bool todoUpdate(uint32_t id, const char* title, const char* description,
                uint32_t dueEpoch, uint32_t reminderEpoch);
bool todoRemoveById(uint32_t id);
bool todoMove(uint32_t id, uint8_t position);
bool todoSetDone(uint32_t id, bool done);
bool todoSetCollapsed(uint32_t id, bool collapsed);

uint32_t todoAddSubtask(uint32_t taskId, const char* title);
bool todoUpdateSubtask(uint32_t taskId, uint32_t subtaskId, const char* title);
bool todoRemoveSubtask(uint32_t taskId, uint32_t subtaskId);
bool todoMoveSubtask(uint32_t taskId, uint32_t subtaskId, uint8_t position);
bool todoSetSubtaskDone(uint32_t taskId, uint32_t subtaskId, bool done);

bool todoMarkReminderFired(uint32_t taskId);
bool todoSnooze(uint32_t taskId, uint32_t reminderEpoch);

// Compatibilidad con los llamadores simples mientras ambas interfaces migran.
bool todoAdd(const char* title);
bool todoToggle(uint8_t index);
bool todoRemove(uint8_t index);
bool todoClearDone();

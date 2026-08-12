#include "todo_model.h"

#include <cassert>
#include <cstring>
#include <iostream>

static TodoItem* task(TodoState& state, uint32_t id) {
    return todoModelFind(state, id);
}

int main() {
    TodoState state;
    todoModelInit(state);
    assert(state.count == 0);
    assert(state.nextId == 1);

    const uint32_t a = todoModelAddTask(state, "  Preparar   demo  ");
    const uint32_t b = todoModelAddTask(state, "Enviar propuesta");
    assert(a == 1 && b == 2);
    assert(std::strcmp(task(state, a)->title, "Preparar demo") == 0);

    assert(todoModelUpdateTask(state, a, "Preparar demo completa",
                               "Incluye capturas\ny notas", 1000, 900));
    assert(std::strcmp(task(state, a)->description, "Incluye capturas\ny notas") == 0);
    assert(task(state, a)->dueEpoch == 1000);
    assert(task(state, a)->reminderEpoch == 900);

    const uint32_t s1 = todoModelAddSubtask(state, a, "Capturar pantalla");
    const uint32_t s2 = todoModelAddSubtask(state, a, "Escribir notas");
    assert(s1 == 3 && s2 == 4);
    assert(task(state, a)->subCount == 2);

    assert(todoModelSetSubtaskDone(state, a, s1, true));
    assert(!task(state, a)->done);
    assert(todoModelSetSubtaskDone(state, a, s2, true));
    assert(task(state, a)->done);

    assert(todoModelSetSubtaskDone(state, a, s1, false));
    assert(!task(state, a)->done);
    assert(todoModelSetTaskDone(state, a, true));
    assert(task(state, a)->subtasks[0].done && task(state, a)->subtasks[1].done);

    assert(todoModelMoveTask(state, b, 0));
    assert(state.items[0].id == b && state.items[1].id == a);

    assert(todoModelMarkReminderFired(state, a));
    assert(task(state, a)->reminderFired);
    assert(todoModelSnooze(state, a, 1500));
    assert(task(state, a)->reminderEpoch == 1500);
    assert(!task(state, a)->reminderFired);

    // Solo interrumpen las tareas pendientes cuyo aviso ya vencio. Una tarea
    // terminada o un aviso ya disparado no pueden reaparecer al reiniciar.
    assert(todoModelFindDueReminder(state, 1499) == nullptr);
    assert(todoModelFindDueReminder(state, 1500) == nullptr);
    assert(todoModelSetTaskDone(state, a, false));
    assert(todoModelFindDueReminder(state, 1500)->id == a);
    assert(todoModelMarkReminderFired(state, a));
    assert(todoModelFindDueReminder(state, 2000) == nullptr);
    assert(todoModelSetTaskDone(state, a, true));
    assert(todoModelSnooze(state, a, 2100));
    assert(todoModelFindDueReminder(state, 2200) == nullptr);

    assert(todoModelRemoveSubtask(state, a, s2));
    assert(task(state, a)->subCount == 1);
    assert(todoModelRemoveTask(state, b));
    assert(state.count == 1 && state.items[0].id == a);

    std::cout << "todo model: OK\n";
    return 0;
}

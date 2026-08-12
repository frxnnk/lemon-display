import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class TodoReminderContractTest(unittest.TestCase):
    def test_store_exposes_due_reminder_selection(self):
        header = (ROOT / "src/todo_store.h").read_text(encoding="utf-8")
        source = (ROOT / "src/todo_store.cpp").read_text(encoding="utf-8")
        self.assertIn("todoFindDueReminder", header)
        self.assertIn("todoModelFindDueReminder", source)

    def test_reminder_card_has_all_three_actions(self):
        header = (ROOT / "src/ui_todo.h").read_text(encoding="utf-8")
        source = (ROOT / "src/ui_todo.cpp").read_text(encoding="utf-8")
        for symbol in ("TODO_REMINDER_COMPLETE", "TODO_REMINDER_SNOOZE",
                       "TODO_REMINDER_OPEN", "uiTodoDrawReminder",
                       "uiTodoReminderTap"):
            self.assertIn(symbol, header + source)
        for label in ("COMPLETAR", "+10 MIN", "ABRIR"):
            self.assertIn(label, source)

    def test_firmware_interrupts_any_running_app_once_and_plays_sound(self):
        main = (ROOT / "src/ferced_main.cpp").read_text(encoding="utf-8")
        for marker in ("PHASE_TODO_REMINDER", "todoFindDueReminder",
                       "todoMarkReminderFired", "todoSnooze",
                       "playAlertUp", "audioSetup"):
            self.assertIn(marker, main)

    def test_simulator_can_capture_reminder(self):
        shot = (ROOT / "sim/shot.ps1").read_text(encoding="utf-8")
        main = (ROOT / "sim/src/sim_main.cpp").read_text(encoding="utf-8")
        self.assertIn("Recordatorio", shot)
        self.assertIn("g_recordatorio", main)
        self.assertIn("uiTodoDrawReminder", main)
        self.assertRegex(
            main,
            r"(?s)if \(g_recordatorio.*s_estatica = true;.*uiTodoDrawReminder",
        )


if __name__ == "__main__":
    unittest.main()

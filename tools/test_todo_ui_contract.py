import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class TodoUiContractTest(unittest.TestCase):
    def test_list_supports_wrapping_scroll_and_expansion_actions(self):
        header = (ROOT / "src/ui_todo.h").read_text(encoding="utf-8")
        source = (ROOT / "src/ui_todo.cpp").read_text(encoding="utf-8")
        for symbol in ("TODO_UI_TOGGLE_TASK", "TODO_UI_TOGGLE_SUBTASK",
                       "TODO_UI_OPEN_DETAIL", "TODO_UI_TOGGLE_COLLAPSE",
                       "uiTodoTap", "uiTodoScroll"):
            self.assertIn(symbol, header)
        self.assertIn("wrapText", source)
        self.assertIn("scrollY", source)
        self.assertNotIn("uiTextoRecortado", source)

    def test_firmware_routes_vertical_gestures_to_task_scroll(self):
        main = (ROOT / "src/ferced_main.cpp").read_text(encoding="utf-8")
        self.assertIn("uiTodoScroll", main)
        self.assertIn("TodoUiAction", main)

    def test_detail_has_full_description_subtasks_and_back_action(self):
        source = (ROOT / "src/ui_todo.cpp").read_text(encoding="utf-8")
        self.assertIn("drawDetail", source)
        self.assertIn("DETAIL_DESCRIPTION", source)
        self.assertIn("TODO_UI_BACK", source)
        self.assertIn("item->description", source)
        self.assertIn("localTimeSafe", source)

    def test_simulator_can_capture_detail_and_scrolled_list(self):
        shot = (ROOT / "sim/shot.ps1").read_text(encoding="utf-8")
        main = (ROOT / "sim/src/sim_main.cpp").read_text(encoding="utf-8")
        self.assertIn("TareaDetalle", shot)
        self.assertIn("TareaScroll", shot)
        self.assertIn("g_tareaDetalle", main)
        self.assertIn("g_tareaScroll", main)
        setup = re.search(r"void setup\(\) \{(?P<body>.*?)\n\}", main, re.DOTALL)
        self.assertIsNotNone(setup)
        body = setup.group("body")
        self.assertRegex(
            body,
            r"(?s)show\(\);\s*if \(g_tareaDetalle.*uiTodoOpen.*"
            r"if \(g_tareaScroll.*uiTodoScroll",
        )


if __name__ == "__main__":
    unittest.main()

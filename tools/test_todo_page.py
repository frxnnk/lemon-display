import pathlib
import re
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def page_source():
    source = (ROOT / "src/todo_page.h").read_text(encoding="utf-8")
    match = re.search(r'R"HTML\((<!doctype html>.*?</html>)\)HTML";', source, re.S)
    if not match:
        raise AssertionError("no se encontró PAGINA")
    return match.group(1)


class TodoPageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = page_source()

    def test_has_complete_editor(self):
        for token in ('id="description"', 'type="datetime-local"',
                      'id="reminder"', 'id="subtasks"', 'Guardar cambios'):
            self.assertIn(token, self.page)

    def test_has_filters_progress_and_accessible_status(self):
        for token in ('data-filter="pending"', 'data-filter="today"',
                      'data-filter="all"', 'data-filter="done"',
                      'id="progress"', 'aria-live="polite"'):
            self.assertIn(token, self.page)

    def test_is_local_and_preserves_editor_during_polling(self):
        self.assertNotRegex(self.page, r'https?://')
        self.assertIn("'X-Ferced':'1'", self.page)
        self.assertRegex(self.page, r'if\s*\(!editing\s*&&\s*!document\.hidden\)')

    def test_touch_targets_and_reduced_motion(self):
        self.assertIn('min-height:44px', self.page)
        self.assertIn('@media (prefers-reduced-motion:reduce)', self.page)

    def test_extractor_builds_browser_preview_with_mock_api(self):
        from extract_todo_page import extract_preview
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "todo-preview.html"
            extract_preview(output)
            preview = output.read_text(encoding="utf-8")
            self.assertIn("mockFetch", preview)
            self.assertIn("Preparar presentación completa", preview)


if __name__ == "__main__":
    unittest.main()

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/web_server.cpp"


class TodoApiContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_exposes_versioned_document_with_rich_fields(self):
        for token in ('\\"v\\":2', '\\"rev\\":', '\\"id\\":', '\\"title\\":',
                      '\\"description\\":', '\\"due\\":', '\\"reminder\\":',
                      '\\"subtasks\\":'):
            self.assertIn(token, self.source)

    def test_all_rich_mutations_exist(self):
        routes = (
            "/api/tasks/create", "/api/tasks/update", "/api/tasks/delete",
            "/api/tasks/done", "/api/tasks/move", "/api/tasks/collapse",
            "/api/tasks/snooze", "/api/subtasks/create",
            "/api/subtasks/update", "/api/subtasks/delete",
            "/api/subtasks/done", "/api/subtasks/move",
        )
        for route in routes:
            self.assertIn(f'"{route}"', self.source)

    def test_mutations_keep_csrf_guard_and_json_errors(self):
        self.assertGreaterEqual(self.source.count("if (!autorizado(req)) return;"), 12)
        self.assertIn('application/json', self.source)
        self.assertRegex(self.source, re.compile(r'errorJSON\s*\('))


if __name__ == "__main__":
    unittest.main()

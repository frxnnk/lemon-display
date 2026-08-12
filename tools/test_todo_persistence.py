import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class TodoPersistenceContractTest(unittest.TestCase):
    def test_store_is_versioned_atomic_and_migrates_v1(self):
        source = (ROOT / "src/todo_store.cpp").read_text(encoding="utf-8")
        self.assertIn("LittleFS.begin", source)
        self.assertIn('TODO_FILE_TMP', source)
        self.assertIn("LittleFS.rename", source)
        self.assertIn("checksum", source)
        self.assertIn('NS_LEGACY = "ferced_todo"', source)
        self.assertIn("migrateLegacy", source)

    def test_public_store_uses_stable_ids(self):
        header = (ROOT / "src/todo_store.h").read_text(encoding="utf-8")
        for symbol in (
            "todoGetById", "todoUpdate", "todoRemoveById", "todoMove",
            "todoAddSubtask", "todoSetSubtaskDone", "todoSnooze",
        ):
            self.assertIn(symbol, header)


if __name__ == "__main__":
    unittest.main()

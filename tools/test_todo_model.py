import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class TodoModelTest(unittest.TestCase):
    def test_native_model_contract(self):
        compiler = shutil.which("g++")
        if not compiler:
            candidates = list((pathlib.Path.home() / "AppData/Local/Microsoft/WinGet/Packages").glob("**/mingw64/bin/g++.exe"))
            self.assertTrue(candidates, "g++ no está instalado")
            compiler = str(candidates[0])
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "todo-model-test.exe"
            build = subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-Isrc", "tools/test_todo_model.cpp", "src/todo_model.cpp",
                 "-o", str(binary)],
                cwd=ROOT, capture_output=True, text=True,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("todo model: OK", run.stdout)


if __name__ == "__main__":
    unittest.main()

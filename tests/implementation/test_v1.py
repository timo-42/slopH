from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest

from sloph.compiler import compile_project
from sloph.core import format_core
from sloph.project import elaborate_project_v1
from sloph.syntax import parse_source_v1


MANIFEST = """format = 0
package = "demo"
source-root = "src"
entry = "demo::main::main"
"""


class SourceV1Tests(unittest.TestCase):
    def _project(self, source: str) -> Path:
        root = Path(tempfile.mkdtemp(prefix="sloph-v1-test-"))
        (root / "src").mkdir()
        (root / "sloph.toml").write_text(MANIFEST, encoding="utf-8")
        (root / "src" / "main.sloph").write_text(source, encoding="ascii")
        self.addCleanup(lambda: __import__("shutil").rmtree(root))
        return root

    def test_nested_comments_and_zero_argument_function(self) -> None:
        source = """/* outer /* nested */ comment */
module demo::main;
fn answer() -> Int { 42 }
value main: Int { answer() }
"""
        module = parse_source_v1(source)
        self.assertEqual((), module.functions[0].parameters)
        project = self._project(source)
        unit = elaborate_project_v1(project)
        rendered = format_core(unit)
        self.assertIn("core::Unit", rendered)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "answer"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"(value 0 (int 42))\n", completed.stdout)

    def test_recursive_source_function_compiles(self) -> None:
        project = self._project(
            """module demo::main;
type Nat {
  Zero();
  Next(rest: Nat);
}
fn count(item: Nat) -> Int {
  case item -> Int {
    Nat::Zero() => { 0 }
    Nat::Next(rest: Nat) => {
      primitive int.add(1, count(rest))
    }
  }
}
value main: Int { count(Nat::Next(Nat::Next(Nat::Zero()))) }
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "count"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"(value 0 (int 2))\n", completed.stdout)

    def test_factorial_uses_enum_bool_case(self) -> None:
        project = self._project(
            """module demo::main;
fn factorial(n: Int) -> Int {
  case primitive int.less(n, 2) -> Int {
    Bool::False() => {
      primitive int.mul(n, factorial(primitive int.sub(n, 1)))
    }
    Bool::True() => { 1 }
  }
}
value main: Int { factorial(6) }
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "factorial"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"(value 0 (int 720))\n", completed.stdout)


if __name__ == "__main__":
    unittest.main()

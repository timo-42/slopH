from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest

from sloph.compiler import compile_project
from sloph.core import evaluate, format_core, format_value, parse_core
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
const main: Int { answer() }
"""
        module = parse_source_v1(source)
        self.assertEqual((), module.functions[0].parameters)
        project = self._project(source)
        unit = elaborate_project_v1(project)
        rendered = format_core(unit)
        self.assertIn("sloph::Unit", rendered)
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
      1 + count(rest)
    }
  }
}
const main: Int { count(Nat::Next(Nat::Next(Nat::Zero()))) }
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
  if n < 2 { 1 } else { n * factorial(n - 1) }
}
const main: Int { factorial(6) }
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "factorial"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"(value 0 (int 720))\n", completed.stdout)

    def test_haskell_style_factorial_clauses(self) -> None:
        project = self._project(
            """module demo::main;
fn factorial(n: Int) -> Int
| 0 => { 1 }
| n => { n * factorial(n - 1) }
const main: Int { factorial(6) }
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "factorial-clauses"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"(value 0 (int 720))\n", completed.stdout)

    def test_clause_requires_exhaustive_catchall(self) -> None:
        with self.assertRaisesRegex(Exception, "catch-all"):
            parse_source_v1(
                "module demo; fn only_zero(n: Int) -> Int | 0 => { 1 }"
            )

    def test_constructor_function_clauses_infer_field_types(self) -> None:
        project = self._project(
            """module demo::main;
type List {
  Nil();
  Cons(head: Int, tail: List);
}
fn sum(xs: List) -> Int
| List::Nil() => { 0 }
| List::Cons(head, tail) => { head + sum(tail) }
const main: Int { sum(List::Cons(2, List::Cons(3, List::Nil()))) }
"""
        )
        unit = elaborate_project_v1(project)
        self.assertIn("demo::main::List::Cons", format_core(unit))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "constructor-clauses"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"(value 0 (int 5))\n", completed.stdout)

    def test_bytes_are_source_core_and_runtime_values(self) -> None:
        project = self._project(
            'module demo::main; const main: Bytes { "hello\\n\\x00\\xff" }'
        )
        unit = elaborate_project_v1(project)
        core = format_core(unit)
        self.assertIn("(bytes x68656c6c6f0a00ff)", core)
        self.assertEqual(core, format_core(parse_core(core.encode("ascii"))))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "bytes"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(
            b"(value 0 (bytes x68656c6c6f0a00ff))\n", completed.stdout
        )

    def test_standard_bytes_library_wraps_core_operations(self) -> None:
        cases = (
            ("core::bytes", "length", '"abc"', "Int", "(value 0 (int 3))\n"),
            ("std::bytes", "is_empty", '""', "Bool", "(value 0 (con sloph::Bool::True))\n"),
        )
        for module, function, literal, result_type, expected in cases:
            with self.subTest(function=function):
                project = self._project(
                    f"""module demo::main;
import {module}::{{{function}}};
const main: {result_type} {{ {function}({literal}) }}
"""
                )
                (project / "sloph.toml").write_text(
                    MANIFEST + 'dependencies = ["std"]\n', encoding="utf-8"
                )
                unit = elaborate_project_v1(project)
                self.assertEqual(
                    expected,
                    format_value(evaluate(unit, "demo::main::main")),
                )

    def test_explicit_generic_function_and_option(self) -> None:
        project = self._project(
            """module demo::main;
fn identity[Item](item: Item) -> Item { item }
const main: Int {
  let option: Option[Int] = Option::Some[Int](identity[Int](42));
  case option -> Int {
    Option::None() => { 0 }
    Option::Some(item) => { item }
  }
}
"""
        )
        unit = elaborate_project_v1(project)
        core = format_core(unit)
        self.assertIn("(forall Item", core)
        self.assertIn("(types Int)", core)
        self.assertEqual(
            "(value 0 (int 42))\n",
            format_value(evaluate(unit, "demo::main::main")),
        )

    def test_selected_public_import_reexports_original_identity(self) -> None:
        project = self._project("module demo::main; const main: Int { 0 }")
        (project / "src" / "defs.sloph").write_text(
            "module demo::defs; public type Answer { Answer(item: Int); }",
            encoding="ascii",
        )
        (project / "src" / "facade.sloph").write_text(
            "module demo::facade; public import demo::defs::{Answer};",
            encoding="ascii",
        )
        (project / "src" / "main.sloph").write_text(
            "module demo::main; import demo::facade::{Answer}; "
            "const main: Answer { Answer::Answer(42) }",
            encoding="ascii",
        )
        unit = elaborate_project_v1(project)
        self.assertEqual(
            "(value 0 (con demo::defs::Answer::Answer (int 42)))\n",
            format_value(evaluate(unit, "demo::main::main")),
        )

    def test_recursive_generic_enum(self) -> None:
        project = self._project(
            """module demo::main;
type List[Item] {
  Nil();
  Cons(head: Item, tail: List[Item]);
}
const main: Int {
  let list: List[Int] = List::Cons[Int](42, List::Nil[Int]());
  case list -> Int {
    List::Nil() => { 0 }
    List::Cons(head, tail) => { head }
  }
}
"""
        )
        self.assertEqual(
            "(value 0 (int 42))\n",
            format_value(evaluate(elaborate_project_v1(project), "demo::main::main")),
        )

    def test_named_functions_are_first_class_and_partially_applied(self) -> None:
        project = self._project(
            """module demo::main;
fn add(left: Int, right: Int) -> Int { left + right }
fn apply(function: fn(Int) -> Int, item: Int) -> Int { function(item) }
const main: Int {
  let increment = add(1);
  apply(increment, 41)
}
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "higher-order"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"(value 0 (int 42))\n", completed.stdout)

    def test_anonymous_closure_captures_outer_value(self) -> None:
        project = self._project(
            """module demo::main;
fn make_adder(base: Int) -> fn(Int) -> Int {
  fn(item: Int) -> Int { base + item }
}
const main: Int { make_adder(40)(2) }
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "closure"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"(value 0 (int 42))\n", completed.stdout)

    def test_if_is_inferred_and_lowered_to_bool_case(self) -> None:
        project = self._project(
            """module demo::main;
fn factorial(n: Int) -> Int {
  if n < 2 { 1 } else { n * factorial(n - 1) }
}
const main: Int { factorial(6) }
"""
        )
        core = format_core(elaborate_project_v1(project))
        self.assertIn("(case", core)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "if"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"(value 0 (int 720))\n", completed.stdout)

    def test_function_main_writes_bytes_and_returns_exit(self) -> None:
        project = self._project(
            """module demo::main;
import os::process::{Exit};
import std::io::{write};
public fn main() -> Exit {
  let written = write("hello\\n");
  Exit::Success()
}
"""
        )
        (project / "sloph.toml").write_text(
            MANIFEST + 'dependencies = ["os", "std"]\n', encoding="utf-8"
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "application"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"hello\n", completed.stdout)

    def test_function_main_failure_sets_process_status(self) -> None:
        project = self._project(
            """module demo::main;
import os::process::{Exit};
public fn main() -> Exit { Exit::Failure(7) }
"""
        )
        (project / "sloph.toml").write_text(
            MANIFEST + 'dependencies = ["os"]\n', encoding="utf-8"
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "failure"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(7, completed.returncode)
        self.assertEqual(b"", completed.stdout)


if __name__ == "__main__":
    unittest.main()

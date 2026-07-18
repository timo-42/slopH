from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from sloph.core import DiagnosticError, Limits, evaluate, format_value
from sloph.project import elaborate_project, load_project, resolve_bundled_packages


MANIFEST = """format = 0
package = "demo"
source-root = "src"
entry = "demo::main::main"
"""

MATH = """module demo::math;
public type Maybe {
  Some(item: Int);
  None();
}
public fn add(left: Int, right: Int) -> Int {
  primitive int.add(left, right)
}
public value base: Int { 40 }
"""

MAIN = """module demo::main;
import demo::math::{Maybe, add, base};
value main: Int {
  let sum: Int = add(base, 2);
  case Maybe::Some(sum) -> Int {
    Maybe::Some(item: Int) => { item }
    Maybe::None() => { 0 }
  }
}
"""


class ProjectTests(unittest.TestCase):
    def test_bundled_packages_resolve_dependency_first(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for package, dependencies in (
                ("base", []),
                ("middle", ["base"]),
                ("top", ["middle"]),
            ):
                package_root = root / package
                package_root.mkdir()
                (package_root / "library.json").write_text(
                    json.dumps(
                        {"dependencies": dependencies, "format": 0, "package": package},
                        sort_keys=True,
                        separators=(",", ":"),
                    ),
                    encoding="ascii",
                )
            with patch("sloph.project.load.libraries_root", return_value=root):
                resolved = resolve_bundled_packages(("top",))
        self.assertEqual(["base", "middle", "top"], [name for name, _ in resolved])

    def _project(self, files: dict[str, str] | None = None) -> Path:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        (root / "sloph.toml").write_text(MANIFEST, encoding="utf-8")
        source = root / "src"
        source.mkdir()
        for relative, content in (files or {"math.sloph": MATH, "main.sloph": MAIN}).items():
            destination = source / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(content, encoding="ascii")
        return root

    def test_loads_in_deterministic_dependency_order(self) -> None:
        project = load_project(self._project())
        self.assertEqual(
            ("demo::math", "demo::main"),
            tuple(module.name for module in project.modules),
        )

    def test_elaborates_modules_and_evaluates_entry(self) -> None:
        unit = elaborate_project(self._project())
        self.assertEqual(
            "(value 0 (int 42))\n",
            format_value(evaluate(unit, "demo::main::main")),
        )
        self.assertEqual(
            ["demo::math::Maybe"], [declaration.name for declaration in unit.types]
        )

    def test_private_selected_import_is_rejected(self) -> None:
        private_math = MATH.replace("public value base", "value base")
        with self.assertRaises(DiagnosticError) as raised:
            elaborate_project(
                self._project({"math.sloph": private_math, "main.sloph": MAIN})
            )
        self.assertEqual(
            "project.resolve.private_import", raised.exception.diagnostic.code
        )

    def test_module_path_mismatch_is_rejected(self) -> None:
        with self.assertRaises(DiagnosticError) as raised:
            load_project(
                self._project(
                    {"wrong.sloph": "module demo::different; value main: Int { 0 }"}
                )
            )
        self.assertEqual(
            "project.module.path_mismatch", raised.exception.diagnostic.code
        )

    def test_import_cycle_is_rejected(self) -> None:
        first = """module demo::first;
import demo::second::{second};
public value first: Int { second }
"""
        second = """module demo::second;
import demo::first::{first};
public value second: Int { first }
"""
        dependent = """module demo::dependent;
import demo::first::{first};
public value dependent: Int { first }
"""
        with self.assertRaises(DiagnosticError) as raised:
            load_project(
                self._project(
                    {
                        "first.sloph": first,
                        "second.sloph": second,
                        "dependent.sloph": dependent,
                    }
                )
            )
        self.assertEqual("project.import.cycle", raised.exception.diagnostic.code)
        self.assertEqual(
            ["demo::first", "demo::second"],
            raised.exception.diagnostic.details["modules"],
        )

    def test_entry_must_be_printable_data(self) -> None:
        manifest = MANIFEST.replace("demo::main::main", "demo::main::identity")
        main = """module demo::main;
fn identity(item: Int) -> Int { item }
value main: Int { 0 }
"""
        root = self._project({"main.sloph": main})
        (root / "sloph.toml").write_text(manifest, encoding="utf-8")
        with self.assertRaises(DiagnosticError) as raised:
            elaborate_project(root)
        self.assertEqual("project.entry.function", raised.exception.diagnostic.code)

    def test_zero_parameter_function_is_rejected(self) -> None:
        main = """module demo::main;
fn main() -> Int { 0 }
value fallback: Int { 0 }
"""
        root = self._project({"main.sloph": main})
        manifest = MANIFEST.replace("demo::main::main", "demo::main::fallback")
        (root / "sloph.toml").write_text(manifest, encoding="utf-8")
        with self.assertRaises(DiagnosticError) as raised:
            elaborate_project(root)
        self.assertEqual("syntax.validate.function_arity", raised.exception.diagnostic.code)

    def test_function_calls_must_be_saturated(self) -> None:
        prefix = """module demo::main;
fn add(left: Int, right: Int) -> Int {
  primitive int.add(left, right)
}
"""
        for call in ("add(1)", "add(1, 2, 3)"):
            with self.subTest(call=call):
                with self.assertRaises(DiagnosticError) as raised:
                    elaborate_project(
                        self._project(
                            {"main.sloph": prefix + f"value main: Int {{ {call} }}\n"}
                        )
                    )
                self.assertEqual(
                    "project.resolve.call_arity", raised.exception.diagnostic.code
                )

    def test_dynamic_call_is_rejected(self) -> None:
        main = """module demo::main;
fn identity(item: Int) -> Int { item }
value main: Int { identity(1)(2) }
"""
        with self.assertRaises(DiagnosticError) as raised:
            elaborate_project(self._project({"main.sloph": main}))
        self.assertEqual("syntax.validate.dynamic_call", raised.exception.diagnostic.code)

    def test_function_cannot_escape(self) -> None:
        main = """module demo::main;
fn identity(item: Int) -> Int { item }
value main: Int { identity }
"""
        with self.assertRaises(DiagnosticError) as raised:
            elaborate_project(self._project({"main.sloph": main}))
        self.assertEqual(
            "project.resolve.escaping_function", raised.exception.diagnostic.code
        )

    def test_source_read_is_bounded_before_parse(self) -> None:
        root = self._project(
            {"main.sloph": "module demo::main; value main: Int { 0 }"}
        )
        with self.assertRaises(DiagnosticError) as raised:
            load_project(root, Limits(input_bytes=8))
        self.assertEqual("project.source.limit", raised.exception.diagnostic.code)

    def test_project_file_count_is_bounded(self) -> None:
        with self.assertRaises(DiagnosticError) as raised:
            load_project(self._project(), Limits(project_files=1))
        self.assertEqual("project.files.limit", raised.exception.diagnostic.code)

    def test_manifest_read_is_bounded(self) -> None:
        root = self._project()
        with (root / "sloph.toml").open("a", encoding="utf-8") as stream:
            stream.write("#" * 66_000)
        with self.assertRaises(DiagnosticError) as raised:
            load_project(root)
        self.assertEqual("project.manifest.limit", raised.exception.diagnostic.code)

    def test_manifest_names_follow_source_casing(self) -> None:
        for old, new in (("demo", "Demo"), ("demo::main::main", "demo::Main::main")):
            with self.subTest(value=new):
                root = self._project()
                path = root / "sloph.toml"
                path.write_text(MANIFEST.replace(old, new), encoding="utf-8")
                with self.assertRaises(DiagnosticError) as raised:
                    load_project(root)
                self.assertIn(
                    raised.exception.diagnostic.code,
                    {"project.manifest.package", "project.manifest.entry"},
                )


if __name__ == "__main__":
    unittest.main()

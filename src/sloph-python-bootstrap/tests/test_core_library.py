from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest

from sloph.compiler import compile_project
from sloph.core import format_core
from sloph.project import elaborate_project_v1, load_project
from sloph.core import DiagnosticError


class CoreLibraryTests(unittest.TestCase):
    def _project(self, expression: str) -> Path:
        temporary = tempfile.TemporaryDirectory(prefix="sloph-core-library-")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        (root / "src").mkdir()
        (root / "sloph.toml").write_text(
            'format=0\npackage="demo"\nsource-root="src"\nentry="demo::main::main"\n',
            encoding="ascii",
        )
        (root / "src" / "main.sloph").write_text(
            "module demo::main;\n"
            "import sloph::{not, equal, not_equal, xor};\n"
            f"const main: Bool {{ {expression} }}\n",
            encoding="ascii",
        )
        return root

    def _run(self, expression: str) -> bytes:
        project = self._project(expression)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "program"
            compile_project(project, output, source_version=1)
            return subprocess.run(
                [output], check=True, capture_output=True
            ).stdout

    def test_language_types_and_core_intrinsics_are_separate(self) -> None:
        project = load_project(self._project("Bool::True()"), source_version=1)
        core_module = next(module for module in project.modules if module.name == "core")
        self.assertTrue(core_module.bundled)
        unit = elaborate_project_v1(project)
        rendered = format_core(unit)
        self.assertEqual(1, rendered.count("(enum sloph::Bool"))
        self.assertEqual(1, rendered.count("(enum sloph::Unit"))
        self.assertNotIn("(enum core::Bytes", rendered)
        self.assertIn("(ctor sloph::Unit::Unit)", rendered)
        self.assertIn("(def sloph::not\n", rendered)
        self.assertIn("(def core::int::add\n", rendered)
        self.assertIn("(fn Bytes Int)", rendered)

    def test_boolean_operations_cover_their_truth_tables(self) -> None:
        true = b"(value 0 (con sloph::Bool::True))\n"
        false = b"(value 0 (con sloph::Bool::False))\n"
        cases = (
            ("not(Bool::False())", true),
            ("not(Bool::True())", false),
            ("equal(Bool::False(), Bool::False())", true),
            ("equal(Bool::False(), Bool::True())", false),
            ("not_equal(Bool::True(), Bool::False())", true),
            ("not_equal(Bool::True(), Bool::True())", false),
            ("xor(Bool::False(), Bool::True())", true),
            ("xor(Bool::True(), Bool::True())", false),
        )
        for expression, expected in cases:
            with self.subTest(expression=expression):
                self.assertEqual(expected, self._run(expression))

    def test_application_cannot_declare_core_intrinsic(self) -> None:
        project = self._project("Bool::True()")
        (project / "src" / "main.sloph").write_text(
            "module demo::main;\n"
            "public intrinsic fn add(left: Int, right: Int) -> Int = int.add;\n"
            "const main: Bool { Bool::True() }\n",
            encoding="ascii",
        )
        with self.assertRaises(DiagnosticError) as raised:
            elaborate_project_v1(project)
        self.assertEqual("project.resolve.trusted_intrinsic", raised.exception.diagnostic.code)

    def test_application_cannot_declare_core_intrinsic_type(self) -> None:
        project = self._project("Bool::True()")
        (project / "src" / "main.sloph").write_text(
            "module demo::main;\n"
            "public intrinsic type Fake;\n"
            "const main: Bool { Bool::True() }\n",
            encoding="ascii",
        )
        with self.assertRaises(DiagnosticError) as raised:
            elaborate_project_v1(project)
        self.assertEqual("project.resolve.trusted_intrinsic", raised.exception.diagnostic.code)


if __name__ == "__main__":
    unittest.main()

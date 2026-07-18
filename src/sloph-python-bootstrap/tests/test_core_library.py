from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest

from sloph.compiler import compile_project
from sloph.core import format_core
from sloph.project import elaborate_project_v1, load_project


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
            "import core::{not, equal, not_equal, xor};\n"
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

    def test_bool_is_declared_by_the_implicitly_loaded_core_library(self) -> None:
        project = load_project(self._project("Bool::True()"), source_version=1)
        core_module = next(module for module in project.modules if module.name == "core")
        self.assertTrue(core_module.bundled)
        unit = elaborate_project_v1(project)
        rendered = format_core(unit)
        self.assertEqual(1, rendered.count("(enum core::Bool"))
        self.assertIn("(def core::not\n", rendered)
        self.assertIn("(def core::equal\n", rendered)
        self.assertIn("(def core::not_equal\n", rendered)
        self.assertIn("(def core::xor\n", rendered)

    def test_boolean_operations_cover_their_truth_tables(self) -> None:
        true = b"(value 0 (con core::Bool::True))\n"
        false = b"(value 0 (con core::Bool::False))\n"
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


if __name__ == "__main__":
    unittest.main()

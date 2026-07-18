from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest

from sloph.compiler import compile_project
from sloph.project import load_project


class MathLibraryTests(unittest.TestCase):
    def _project(self, expression: str, result_type: str = "Int") -> Path:
        temporary = tempfile.TemporaryDirectory(prefix="sloph-math-library-")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        (root / "src").mkdir()
        (root / "sloph.toml").write_text(
            'format=0\npackage="demo"\nsource-root="src"\n'
            'entry="demo::main::main"\ndependencies=["math"]\n',
            encoding="ascii",
        )
        (root / "src" / "main.sloph").write_text(
            "module demo::main;\n"
            "import math::int::{absolute, minimum, maximum, sign, distance, "
            "is_negative, is_positive};\n"
            f"const main: {result_type} {{ {expression} }}\n",
            encoding="ascii",
        )
        return root

    def _run(self, expression: str, result_type: str = "Int") -> bytes:
        project = self._project(expression, result_type)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "program"
            compile_project(project, output, source_version=1)
            return subprocess.run(
                [output], check=True, capture_output=True
            ).stdout

    def test_math_is_an_independent_explicit_dependency(self) -> None:
        project = load_project(self._project("absolute(0)"), source_version=1)
        modules = {module.name for module in project.modules}
        self.assertIn("core", modules)
        self.assertIn("math::int", modules)
        self.assertNotIn("std::bytes", modules)
        self.assertNotIn("syscall::posix", modules)

    def test_integer_helpers(self) -> None:
        cases = (
            ("absolute(0 - 7)", 7),
            ("absolute(7)", 7),
            ("minimum(3, 9)", 3),
            ("minimum(9, 3)", 3),
            ("maximum(3, 9)", 9),
            ("maximum(9, 3)", 9),
            ("sign(0 - 7)", -1),
            ("sign(0)", 0),
            ("sign(7)", 1),
            ("distance(3, 9)", 6),
            ("distance(9, 3)", 6),
            ("distance(4, 4)", 0),
        )
        for expression, expected in cases:
            with self.subTest(expression=expression):
                self.assertEqual(
                    f"(value 0 (int {expected}))\n".encode("ascii"),
                    self._run(expression),
                )

    def test_integer_predicates(self) -> None:
        true = b"(value 0 (con core::Bool::True))\n"
        false = b"(value 0 (con core::Bool::False))\n"
        cases = (
            ("is_negative(0 - 1)", true),
            ("is_negative(0)", false),
            ("is_negative(1)", false),
            ("is_positive(0 - 1)", false),
            ("is_positive(0)", false),
            ("is_positive(1)", true),
        )
        for expression, expected in cases:
            with self.subTest(expression=expression):
                self.assertEqual(expected, self._run(expression, "Bool"))


if __name__ == "__main__":
    unittest.main()

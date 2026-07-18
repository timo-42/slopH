from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from sloph.core import DiagnosticError
from sloph.core.model import ConExpr, LamExpr
from sloph.project import CompilerSpecials, elaborate_project_v1, load_project
from sloph.syntax import (
    format_source,
    parse_source_v1,
    syntax_from_json,
    syntax_to_json,
)


class PlatformSelectionTests(unittest.TestCase):
    def test_conditional_import_syntax_round_trips(self) -> None:
        source = """module demo::main when SPECIAL_ARCH amd64;
import case SPECIAL_PLATFORM {
  linux, amd64 => demo::linux::{native};
  darwin, arm64 => demo::darwin::{native};
}
public fn main() -> Bool { native() }
"""
        parsed = parse_source_v1(source)
        rendered = format_source(parsed, version=1)
        reparsed = parse_source_v1(rendered)
        self.assertEqual(rendered, format_source(reparsed, version=1))
        encoded = syntax_to_json(parsed, version=1)
        self.assertEqual(
            encoded, syntax_to_json(syntax_from_json(encoded, version=1), version=1)
        )

    def test_missing_conditional_import_branch_is_diagnostic(self) -> None:
        with self._project(
            'dependencies=[]',
            """module demo::main;
import case SPECIAL_PLATFORM {
  linux, amd64 => demo::linux::{native};
}
public fn main() -> Exit { Exit::Success() }
""",
        ) as root:
            with self.assertRaises(DiagnosticError) as caught:
                load_project(
                    root,
                    source_version=1,
                    specials=CompilerSpecials("darwin", "arm64"),
                )
        self.assertEqual("project.special.no_match", caught.exception.diagnostic.code)

    def test_architecture_module_is_unavailable_on_wrong_arch(self) -> None:
        source = """module demo::main;
import cpu::amd64::{avx512};
public fn main() -> Exit { Exit::Success() }
"""
        with self._project('dependencies=["cpu"]', source) as root:
            with self.assertRaises(DiagnosticError) as caught:
                load_project(
                    root,
                    source_version=1,
                    specials=CompilerSpecials("darwin", "arm64"),
                )
        self.assertEqual("project.module.unavailable", caught.exception.diagnostic.code)

    def test_amd64_avx512_is_conservatively_false(self) -> None:
        source = """module demo::main;
import cpu::amd64::{avx512};
public fn main() -> Exit {
  if avx512() { Exit::Failure(1) } else { Exit::Success() }
}
"""
        with self._project('dependencies=["cpu"]', source) as root:
            project = load_project(
                root,
                source_version=1,
                specials=CompilerSpecials("linux", "amd64"),
            )
            unit = elaborate_project_v1(project)
        feature = next(
            item for item in unit.definitions if item.name == "cpu::amd64::avx512"
        )
        self.assertIsInstance(feature.value, LamExpr)
        self.assertIsInstance(feature.value.body, ConExpr)
        self.assertEqual("core::Bool::False", feature.value.body.constructor)

    def _project(self, dependencies: str, source: str):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        (root / "src").mkdir()
        (root / "sloph.toml").write_text(
            "format=0\npackage=\"demo\"\nsource-root=\"src\"\n"
            f"entry=\"demo::main::main\"\n{dependencies}\n",
            encoding="ascii",
        )
        (root / "src" / "main.sloph").write_text(source, encoding="ascii")

        class _Context:
            def __enter__(self): return root
            def __exit__(self, *_): temporary.cleanup()

        return _Context()


if __name__ == "__main__":
    unittest.main()

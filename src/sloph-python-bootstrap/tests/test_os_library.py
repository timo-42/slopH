from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from sloph.core import DiagnosticError, format_core
from sloph.project import elaborate_project_v1, load_project


class OsLibraryTests(unittest.TestCase):
    def _project(self, source: str, dependencies: str = "") -> Path:
        temporary = tempfile.TemporaryDirectory(prefix="sloph-os-library-")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        (root / "src").mkdir()
        (root / "sloph.toml").write_text(
            'format=0\npackage="demo"\nsource-root="src"\n'
            'entry="demo::main::main"\n'
            f"dependencies=[{dependencies}]\n",
            encoding="ascii",
        )
        (root / "src" / "main.sloph").write_text(source, encoding="ascii")
        return root

    def test_exit_is_declared_by_the_explicit_os_library(self) -> None:
        project = load_project(
            self._project(
                """module demo::main;
import os::process::{Exit};
public fn main() -> Exit { Exit::Success() }
""",
                '"os"',
            ),
            source_version=1,
        )
        process_module = next(
            module for module in project.modules if module.name == "os::process"
        )
        self.assertTrue(process_module.bundled)
        rendered = format_core(elaborate_project_v1(project))
        self.assertEqual(1, rendered.count("(enum os::process::Exit"))
        self.assertNotIn("core::Exit", rendered)

    def test_os_dependency_is_required(self) -> None:
        project = self._project(
            """module demo::main;
import os::process::{Exit};
public fn main() -> Exit { Exit::Success() }
"""
        )
        with self.assertRaises(DiagnosticError) as caught:
            load_project(project, source_version=1)
        self.assertEqual("project.import.missing", caught.exception.diagnostic.code)

    def test_exit_import_is_required(self) -> None:
        project = self._project(
            "module demo::main; public fn main() -> Exit { Exit::Success() }",
            '"os"',
        )
        with self.assertRaises(DiagnosticError) as caught:
            elaborate_project_v1(project)
        self.assertEqual("project.resolve.unknown_name", caught.exception.diagnostic.code)

    def test_old_core_exit_identity_is_rejected(self) -> None:
        project = self._project(
            "module demo::main; public fn main() -> core::Exit { core::Exit::Success() }"
        )
        with self.assertRaises(DiagnosticError) as caught:
            elaborate_project_v1(project)
        self.assertEqual("project.resolve.unknown_name", caught.exception.diagnostic.code)


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest

from sloph.compiler import compile_project
from sloph.core.diagnostics import DiagnosticError
from sloph.project import CompilerTarget, elaborate_project_v1


class ExplicitMemoryTests(unittest.TestCase):
    def _project(self, body: str) -> Path:
        root = Path(tempfile.mkdtemp(prefix="sloph-memory-test-"))
        (root / "src").mkdir()
        (root / "sloph.toml").write_text(
            """format = 0
package = "memory_case"
source-root = "src"
entry = "memory_case::main::main"
dependencies = ["memory"]
""",
            encoding="ascii",
        )
        (root / "src" / "main.sloph").write_text(body, encoding="ascii")
        self.addCleanup(lambda: __import__("shutil").rmtree(root))
        return root

    def test_page_backed_buffer_is_explicitly_dropped(self) -> None:
        project = self._project(
            """module memory_case::main;
import memory::{AllocationError, Allocator, Buffer, allocate, capacity, drop, system};
fn make(allocator: borrow Allocator) -> Result[Buffer, AllocationError] {
  let buffer = allocate(allocator, 123)?;
  Result::Ok[Buffer, AllocationError](buffer)
}
fn checked(allocator: borrow Allocator) -> Result[Int, AllocationError] {
  let buffer = make(allocator)?;
  defer drop(buffer);
  Result::Ok[Int, AllocationError](capacity(buffer))
}
const main: Result[Int, AllocationError] { checked(system()) }
"""
        )
        unit = elaborate_project_v1(project)
        self.assertEqual(3, unit.version)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "memory"
            compile_project(project, output, source_version=1)
            completed = subprocess.run([output], check=False, capture_output=True)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(
            b"(value 0 (con sloph::Result::Ok (int 123)))\n",
            completed.stdout,
        )
        self.assertEqual(b"", completed.stderr)

    def test_missing_explicit_drop_is_rejected(self) -> None:
        project = self._project(
            """module memory_case::main;
import memory::{AllocationError, Allocator, allocate, capacity, system};
fn leaked(allocator: borrow Allocator) -> Result[Int, AllocationError] {
  let buffer = allocate(allocator, 64)?;
  Result::Ok[Int, AllocationError](capacity(buffer))
}
const main: Result[Int, AllocationError] { leaked(system()) }
"""
        )
        with self.assertRaises(DiagnosticError) as raised:
            elaborate_project_v1(project)
        self.assertEqual("core.validate.owned_not_consumed", raised.exception.diagnostic.code)

    def test_duplicate_deferred_drop_is_rejected(self) -> None:
        project = self._project(
            """module memory_case::main;
import memory::{AllocationError, Allocator, allocate, capacity, drop, system};
fn invalid(allocator: borrow Allocator) -> Result[Int, AllocationError] {
  let buffer = allocate(allocator, 64)?;
  defer drop(buffer);
  defer drop(buffer);
  Result::Ok[Int, AllocationError](capacity(buffer))
}
const main: Result[Int, AllocationError] { invalid(system()) }
"""
        )
        with self.assertRaises(DiagnosticError) as raised:
            elaborate_project_v1(project)
        self.assertEqual("core.validate.use_after_move", raised.exception.diagnostic.code)

    def test_owned_resource_constructor_is_not_exported(self) -> None:
        project = self._project(
            """module memory_case::main;
import memory::{Buffer};
const main: Buffer { Buffer::Buffer(0, 0, 0) }
"""
        )
        with self.assertRaises(DiagnosticError) as raised:
            elaborate_project_v1(project)
        self.assertEqual("project.resolve.unknown_constructor", raised.exception.diagnostic.code)

    def test_application_cannot_import_native_page_adapter(self) -> None:
        target = CompilerTarget.host()
        provider = f"syscall::memory::{target.os.value}::{target.arch.value}"
        project = self._project(
            f"""module memory_case::main;
import {provider}::{{native_map_pages}};
const main: Int {{ 0 }}
"""
        )
        with self.assertRaises(DiagnosticError) as raised:
            elaborate_project_v1(project)
        self.assertEqual("project.resolve.foreign_import", raised.exception.diagnostic.code)


if __name__ == "__main__":
    unittest.main()

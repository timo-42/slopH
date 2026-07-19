from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest
import json

from sloph.backend import emit_c
from sloph.core import format_core, parse_core
from sloph.core import DiagnosticError
from sloph.project import Arch, CompilerTarget, OS, elaborate_project_v1, load_project
from sloph.project.load import _load_module_provider
from sloph.project.model import ProjectModule


ROOT = Path(__file__).resolve().parents[3]
COMPONENT = ROOT / "src" / "sloph-python-bootstrap"
LIBRARY = ROOT / "src" / "libraries" / "syscall"
CC = "/usr/bin/cc"
TARGET = CompilerTarget.host()
PLATFORM_ROOT = LIBRARY / "src" / "posix" / TARGET.os.value / TARGET.arch.value
SHARED_LIBRARY = PLATFORM_ROOT / (
    "libsloph_syscall.dylib" if TARGET.os == OS.DARWIN else "libsloph_syscall.so"
)


class PosixLibraryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        subprocess.run([str(LIBRARY / "build.sh")], cwd=LIBRARY, check=True)

    def test_application_cannot_declare_foreign_binding(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "sloph.json").write_text(
                '{"format":1,"package":"demo","source-root":"src",'
                '"entry":"demo::main::main","dependencies":["syscall"]}\n',
                encoding="ascii",
            )
            (root / "src" / "main.sloph").write_text(
                'module demo::main; public foreign fn fake(item: Int) -> Int = foreign.demo.write; const main: Int { fake(1) }',
                encoding="ascii",
            )
            with self.assertRaises(DiagnosticError) as caught:
                elaborate_project_v1(load_project(root, source_version=1))
        self.assertEqual("project.resolve.foreign_signature", caught.exception.diagnostic.code)

    def test_legacy_provider_sources_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module_path = root / "native.sloph"
            module_path.write_text("module demo::native;", encoding="ascii")
            provider_root = root / "native"
            provider_root.mkdir()
            (provider_root / "provider.json").write_text(
                json.dumps(
                    {
                        "bindings": "bindings.json",
                        "format": 0,
                        "module": "demo::native",
                        "sources": ["native.S"],
                    }
                ),
                encoding="ascii",
            )
            module = ProjectModule("demo::native", module_path, None, (), True)
            with self.assertRaises(DiagnosticError) as caught:
                _load_module_provider(module)
        self.assertEqual("project.provider.shape", caught.exception.diagnostic.code)

    def test_platform_provider_exposes_read_and_write(self) -> None:
        harness = r'''#include "syscall.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
int main(void) {
  int descriptors[2]; char result[4] = {0};
  if (pipe(descriptors) != 0) return 1;
  if (sloph_syscall_write(descriptors[1], "abc", 3) != 3) return 2;
  if (sloph_syscall_read(descriptors[0], result, 3) != 3) return 3;
  if (memcmp(result, "abc", 3) != 0) return 4;
  errno = 0;
  if (sloph_syscall_write(-1, "x", 1) != -1 || errno != EBADF) return 5;
  errno = 0;
  if (sloph_syscall_read(-1, result, 1) != -1 || errno != EBADF) return 6;
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "harness.c"
            output = root / "harness"
            source.write_text(harness, encoding="ascii")
            subprocess.run(
                [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(PLATFORM_ROOT), str(source), str(SHARED_LIBRARY), f"-Wl,-rpath,{PLATFORM_ROOT}", "-o", str(output)],
                check=True, capture_output=True,
            )
            completed = subprocess.run([output], check=False)
        self.assertEqual(0, completed.returncode)

    def test_binding_metadata_round_trips_through_core(self) -> None:
        project = load_project(ROOT / "examples" / "hello-world", source_version=1)
        unit = elaborate_project_v1(project)
        text = format_core(unit)
        self.assertIn(
            f"(binding foreign.syscall.posix.{TARGET.os.value}.{TARGET.arch.value}.write",
            text,
        )
        self.assertIn(
            f"(provider syscall::posix::{TARGET.os.value}::{TARGET.arch.value})", text
        )
        self.assertIn("(effects io)", text)
        self.assertIn("(fact pointer_retention none)", text)
        self.assertEqual(text, format_core(parse_core(text)))

    def test_sloph_selects_exactly_one_platform_provider(self) -> None:
        cases = (
            (CompilerTarget(OS.LINUX, Arch.AMD64), "syscall::posix::linux::amd64"),
            (CompilerTarget(OS.DARWIN, Arch.ARM64), "syscall::posix::darwin::arm64"),
        )
        for target, expected in cases:
            with self.subTest(expected=expected):
                project = load_project(
                    ROOT / "examples" / "hello-world",
                    source_version=1,
                    target=target,
                )
                modules = {module.name for module in project.modules}
                providers = {binding.provider for binding in project.foreign_bindings}
                self.assertIn(expected, modules)
                self.assertEqual({expected}, providers)
                self.assertEqual(2, elaborate_project_v1(project).version)
                self.assertNotIn(
                    "syscall::posix::darwin::arm64"
                    if target.os == OS.LINUX
                    else "syscall::posix::linux::amd64",
                    modules,
                )

    def test_std_write_retries_short_write_and_eintr(self) -> None:
        unit = elaborate_project_v1(
            load_project(ROOT / "examples" / "hello-world", source_version=1)
        )
        generated = emit_c(unit, "hello::main::main")
        provider = r'''#include "syscall.h"
#include <errno.h>
#include <unistd.h>
static unsigned calls;
ssize_t sloph_syscall_read(int fd, void *buffer, size_t count) { return read(fd, buffer, count); }
ssize_t sloph_syscall_write(int fd, const void *buffer, size_t count) {
  calls++;
  if (calls == 2) { errno = EINTR; return -1; }
  if (count > 3) count = 3;
  return write(fd, buffer, count);
}
'''
        completed = self._compile_generated(generated, provider)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"Hello, world!\n", completed.stdout)

    def test_std_write_traps_on_permanent_error(self) -> None:
        unit = elaborate_project_v1(
            load_project(ROOT / "examples" / "hello-world", source_version=1)
        )
        generated = emit_c(unit, "hello::main::main")
        provider = r'''#include "syscall.h"
#include <errno.h>
ssize_t sloph_syscall_read(int fd, void *buffer, size_t count) { (void)fd; (void)buffer; (void)count; errno=EBADF; return -1; }
ssize_t sloph_syscall_write(int fd, const void *buffer, size_t count) { (void)fd; (void)buffer; (void)count; errno=EBADF; return -1; }
'''
        completed = self._compile_generated(generated, provider)
        self.assertEqual(2, completed.returncode)
        self.assertIn(b"sloph trap: POSIX write failed", completed.stderr)

    def test_try_write_returns_permanent_and_no_progress_errors(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "sloph.json").write_text(
                '{"format":1,"package":"demo","source-root":"src",'
                '"entry":"demo::main::main","dependencies":["os","std"]}\n',
                encoding="ascii",
            )
            (root / "src" / "main.sloph").write_text(
                """module demo::main;
import os::process::{Exit};
import std::io::{try_write};
public fn main() -> Exit {
  let result = try_write("x");
  case result -> Exit {
    Result::Ok(ignored_unit) => { Exit::Success() }
    Result::Err(error) => { Exit::Failure(7) }
  }
}
""",
                encoding="ascii",
            )
            unit = elaborate_project_v1(load_project(root, source_version=1))
        generated = emit_c(unit, "demo::main::main")
        providers = (
            r'''#include "syscall.h"
#include <errno.h>
ssize_t sloph_syscall_read(int fd, void *buffer, size_t count) { (void)fd; (void)buffer; (void)count; return -1; }
ssize_t sloph_syscall_write(int fd, const void *buffer, size_t count) { (void)fd; (void)buffer; (void)count; errno=EBADF; return -1; }
''',
            r'''#include "syscall.h"
ssize_t sloph_syscall_read(int fd, void *buffer, size_t count) { (void)fd; (void)buffer; (void)count; return -1; }
ssize_t sloph_syscall_write(int fd, const void *buffer, size_t count) { (void)fd; (void)buffer; (void)count; return 0; }
''',
        )
        for provider in providers:
            with self.subTest(provider=provider):
                completed = self._compile_generated(generated, provider)
                self.assertEqual(7, completed.returncode)
                self.assertNotIn(b"sloph trap", completed.stderr)

    def _compile_generated(self, generated: str, provider: str) -> subprocess.CompletedProcess[bytes]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            program = root / "program.c"
            boundary = root / "boundary.c"
            output = root / "program"
            program.write_text(generated, encoding="ascii")
            boundary.write_text(provider, encoding="ascii")
            subprocess.run(
                [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(PLATFORM_ROOT), str(program), str(boundary), "-o", str(output)],
                check=True, capture_output=True,
            )
            return subprocess.run([output], check=False, capture_output=True)


if __name__ == "__main__":
    unittest.main()

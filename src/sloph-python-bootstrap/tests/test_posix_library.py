from __future__ import annotations

from pathlib import Path
import platform
import subprocess
import tempfile
import unittest

from sloph.backend import emit_c
from sloph.core import format_core, parse_core
from sloph.core import DiagnosticError
from sloph.project import elaborate_project_v1, load_project


COMPONENT = Path(__file__).resolve().parents[1]
ROOT = COMPONENT.parents[1]
LIBRARY = COMPONENT.parent / "libraries" / "syscall"
CC = "/usr/bin/cc"
PLATFORM = "macos" if platform.system() == "Darwin" else "linux"
PLATFORM_ROOT = LIBRARY / "platform" / PLATFORM


class PosixLibraryTests(unittest.TestCase):
    def test_application_cannot_invoke_foreign_primitive_directly(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "sloph.toml").write_text(
                'format=0\npackage="demo"\nsource-root="src"\nentry="demo::main::main"\ndependencies=["syscall"]\n',
                encoding="ascii",
            )
            (root / "src" / "main.sloph").write_text(
                'module demo::main; const main: Int { primitive foreign.syscall.posix_write(1, "x", 0, 1) }',
                encoding="ascii",
            )
            with self.assertRaises(DiagnosticError) as caught:
                elaborate_project_v1(load_project(root, source_version=1))
        self.assertEqual("project.resolve.trusted_primitive", caught.exception.diagnostic.code)

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
                [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(PLATFORM_ROOT), str(source), str(PLATFORM_ROOT / "syscall.S"), "-o", str(output)],
                check=True, capture_output=True,
            )
            completed = subprocess.run([output], check=False)
        self.assertEqual(0, completed.returncode)

    def test_binding_metadata_round_trips_through_core(self) -> None:
        project = load_project(ROOT / "examples" / "hello-world", source_version=1)
        unit = elaborate_project_v1(project)
        text = format_core(unit)
        self.assertIn("(binding foreign.syscall.posix_write", text)
        self.assertIn("(effects io)", text)
        self.assertIn("(fact pointer_retention none)", text)
        self.assertEqual(text, format_core(parse_core(text)))

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

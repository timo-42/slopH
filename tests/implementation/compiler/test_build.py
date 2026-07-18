from pathlib import Path
import platform
import subprocess
import tempfile
import time
import unittest

from sloph.backend import emit_c
from sloph.compiler import compile_core
from sloph.core import DiagnosticError, parse_core


@unittest.skipUnless(
    (platform.system(), platform.machine().lower())
    in {("Darwin", "arm64"), ("Linux", "x86_64"), ("Linux", "amd64")},
    "experimental C11 bridge host",
)
class NativeBuildTests(unittest.TestCase):
    def test_exact_integer_program(self) -> None:
        unit = parse_core(
            b"""(core 0 (types) (defs
              (def example::main Int
                (prim int.mul (int 12345678901234567890) (int -9)))))"""
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "program"
            emitted = Path(directory) / "program.c"
            result = compile_core(
                unit, "example::main", output, emit_c_path=emitted
            )
            completed = subprocess.run(
                [str(output)], check=False, capture_output=True, timeout=10
            )
            self.assertEqual(0, completed.returncode)
            self.assertEqual(
                b"(value 0 (int -111111110111111111010))\n",
                completed.stdout,
            )
            self.assertEqual(b"", completed.stderr)
            self.assertGreater(result.c_bytes, 0)
            self.assertEqual(emit_c(unit, "example::main"), emitted.read_text("ascii"))

    def test_host_compiler_diagnostics_are_bounded(self) -> None:
        unit = parse_core(
            b"(core 0 (types) (defs (def example::main Int (int 0))))"
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = root / "noisy-cc"
            fake.write_text(
                "#!/bin/sh\nwhile true; do printf 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx' >&2; done\n",
                encoding="ascii",
            )
            fake.chmod(0o755)
            with self.assertRaises(DiagnosticError) as raised:
                compile_core(unit, "example::main", root / "program", cc=str(fake))
        self.assertEqual(
            "compiler.c11.output_limit", raised.exception.diagnostic.code
        )

    def test_unused_valid_bindings_compile_under_werror(self) -> None:
        unit = parse_core(b"""(core 0
          (types (enum example::Box
            (ctor example::Box::Box (field item Int))))
          (defs
            (def example::ignore (fn Int Int)
              (lam (bind unused Int) (int 7)))
            (def example::main Int
              (case (con example::Box::Box (int 1)) Int
                (alt example::Box::Box (bind field Int)
                  (let (bind dropped Int)
                    (app (global example::ignore) (int 9))
                    (int 42)))))))""")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "program"
            compile_core(unit, "example::main", output)
            completed = subprocess.run([str(output)], capture_output=True, timeout=5)
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"(value 0 (int 42))\n", completed.stdout)

    def test_inherited_compiler_pipes_do_not_hang(self) -> None:
        unit = parse_core(
            b"(core 0 (types) (defs (def example::main Int (int 0))))"
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = root / "forking-cc"
            fake.write_text(
                "#!/bin/sh\nfor argument do output=$argument; done\n"
                "printf '#!/bin/sh\\nexit 0\\n' > \"$output\"\nchmod 755 \"$output\"\n"
                "(sleep 30) &\nexit 0\n",
                encoding="ascii",
            )
            fake.chmod(0o755)
            started = time.monotonic()
            compile_core(unit, "example::main", root / "program", cc=str(fake))
            elapsed = time.monotonic() - started
        self.assertLess(elapsed, 5)

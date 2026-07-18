from pathlib import Path
import platform
import json
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch

from sloph.backend import emit_c
from sloph.compiler import compile_core, compile_project
from sloph.compiler.build import _native_inputs, _run_package_build_scripts
from sloph.core import DiagnosticError, parse_core
from sloph.project import elaborate_project_v1, load_project


ROOT = Path(__file__).resolve().parents[4]


@unittest.skipUnless(
    (platform.system(), platform.machine().lower())
    in {("Darwin", "arm64"), ("Linux", "x86_64"), ("Linux", "amd64")},
    "experimental C11 bridge host",
)
class NativeBuildTests(unittest.TestCase):
    def test_package_build_scripts_run_in_dependency_order(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = root / "record"
            packages = []
            for name in ("dependency", "consumer"):
                package = root / name
                package.mkdir()
                script = package / "build.sh"
                script.write_text(
                    f"#!/bin/sh\nprintf '%s\\n' {name} >> '{record}'\n",
                    encoding="ascii",
                )
                script.chmod(0o755)
                packages.append((name, package))
            _run_package_build_scripts(tuple(packages))
            self.assertEqual("dependency\nconsumer\n", record.read_text("ascii"))

    def test_package_build_script_failure_is_diagnostic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "build.sh"
            script.write_text("#!/bin/sh\necho broken >&2\nexit 7\n", encoding="ascii")
            script.chmod(0o755)
            with self.assertRaises(DiagnosticError) as raised:
                _run_package_build_scripts((("broken", root),))
        self.assertEqual("compiler.build_script.failed", raised.exception.diagnostic.code)
        self.assertEqual(7, raised.exception.diagnostic.details["exit_code"])
        self.assertIn("broken", raised.exception.diagnostic.details["stderr"])

    def test_compile_project_runs_dependency_build_scripts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "native"
            package.mkdir()
            marker = package / "built"
            script = package / "build.sh"
            script.write_text(f"#!/bin/sh\n: > '{marker}'\n", encoding="ascii")
            script.chmod(0o755)
            output = root / "program"
            with patch(
                "sloph.compiler.build.resolve_bundled_packages",
                return_value=(("native", package),),
            ):
                compile_project(
                    ROOT / "examples" / "hello-world",
                    output,
                    source_version=1,
                )
            self.assertTrue(marker.is_file())

    def test_provider_link_inputs_are_shared_libraries(self) -> None:
        syscall = ROOT / "src" / "libraries" / "syscall"
        subprocess.run([str(syscall / "build.sh")], cwd=syscall, check=True)
        project = load_project(ROOT / "examples" / "hello-world", source_version=1)
        unit = elaborate_project_v1(project)
        native_inputs = _native_inputs(unit)
        self.assertTrue(native_inputs)
        self.assertTrue(
            all(path.suffix in {".so", ".dylib"} for _, paths in native_inputs for path in paths)
        )
        self.assertFalse(
            any(path.suffix == ".S" for _, paths in native_inputs for path in paths)
        )

    def test_missing_provider_library_is_diagnostic(self) -> None:
        project = load_project(ROOT / "examples" / "hello-world", source_version=1)
        unit = elaborate_project_v1(project)
        identity = unit.foreign_bindings[0].provider
        with tempfile.TemporaryDirectory() as directory:
            libraries = Path(directory)
            provider = libraries / identity.split("::")[0] / "src" / Path(
                *identity.split("::")[1:]
            )
            provider.mkdir(parents=True)
            suffix = ".dylib" if platform.system() == "Darwin" else ".so"
            (provider / "provider.json").write_text(
                json.dumps(
                    {
                        "bindings": "bindings.json",
                        "format": 0,
                        "libraries": ["missing" + suffix],
                        "module": identity,
                    }
                ),
                encoding="ascii",
            )
            with patch("sloph.compiler.build.libraries_root", return_value=libraries):
                with self.assertRaises(DiagnosticError) as raised:
                    _native_inputs(unit)
        self.assertEqual("compiler.provider.library", raised.exception.diagnostic.code)

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

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
from sloph.compiler import build as compiler_build
from sloph.compiler.build import _native_inputs
from sloph.core import DiagnosticError, parse_core
from sloph.project import elaborate_project_v1, load_project


ROOT = Path(__file__).resolve().parents[4]


@unittest.skipUnless(
    (platform.system(), platform.machine().lower())
    in {("Darwin", "arm64"), ("Linux", "x86_64"), ("Linux", "amd64")},
    "experimental C11 bridge host",
)
class NativeBuildTests(unittest.TestCase):
    def test_package_build_scripts_are_not_supported(self) -> None:
        self.assertFalse(hasattr(compiler_build, "_run_package_build_scripts"))

    def test_bundled_provider_metadata_is_strict_format_one(self) -> None:
        syscall = ROOT / "src" / "libraries" / "syscall"
        manifests = sorted(syscall.rglob("provider.json"))
        self.assertEqual(4, len(manifests))
        for manifest in manifests:
            with self.subTest(manifest=manifest):
                metadata = json.loads(manifest.read_text(encoding="utf-8"))
                self.assertEqual(
                    {"format", "module", "bindings", "sources"}, set(metadata)
                )
                self.assertEqual(1, metadata["format"])
                self.assertTrue(metadata["sources"])

    def test_bundled_providers_have_no_prebuilt_shared_objects(self) -> None:
        syscall = ROOT / "src" / "libraries" / "syscall"
        self.assertEqual([], list(syscall.rglob("*.so")))
        self.assertEqual([], list(syscall.rglob("*.dylib")))
        self.assertFalse((syscall / "build.sh").exists())

    def test_provider_link_inputs_are_native_sources(self) -> None:
        project = load_project(ROOT / "examples" / "hello-world", source_version=1)
        unit = elaborate_project_v1(project)
        native_inputs = _native_inputs(unit)
        self.assertTrue(native_inputs)
        self.assertTrue(
            all(path.suffix in {".c", ".S"} for _, paths in native_inputs for path in paths)
        )
        self.assertTrue(all(path.is_file() for _, paths in native_inputs for path in paths))

    def test_missing_provider_source_is_diagnostic(self) -> None:
        project = load_project(ROOT / "examples" / "hello-world", source_version=1)
        unit = elaborate_project_v1(project)
        identity = unit.foreign_bindings[0].provider
        with tempfile.TemporaryDirectory() as directory:
            libraries = Path(directory)
            provider = libraries / identity.split("::")[0] / "src" / Path(
                *identity.split("::")[1:]
            )
            provider.mkdir(parents=True)
            (provider / "provider.json").write_text(
                json.dumps(
                    {
                        "bindings": "bindings.json",
                        "format": 1,
                        "module": identity,
                        "sources": ["missing.c"],
                    }
                ),
                encoding="ascii",
            )
            with patch("sloph.compiler.build.libraries_root", return_value=libraries):
                with self.assertRaises(DiagnosticError) as raised:
                    _native_inputs(unit)
        self.assertEqual("compiler.provider.source", raised.exception.diagnostic.code)

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

from contextlib import redirect_stderr
import io
import json
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from sloph.cli import main
from sloph.compiler import BuildResult


ROOT = Path(__file__).resolve().parents[3]


class ExperimentalCliTests(unittest.TestCase):
    def test_malformed_ast_json_is_not_an_internal_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ast.json"
            path.write_text(
                '{"schema":"sloph.syntax","version":' + "9" * 5000 + ',"module":{}}',
                encoding="ascii",
            )
            diagnostics = io.StringIO()
            with redirect_stderr(diagnostics):
                result = main(
                    [
                        "--diagnostics", "jsonl", "unstable", "ast", "check",
                        str(path), "--input-format", "json",
                    ]
                )
        self.assertEqual(1, result)
        self.assertEqual("syntax.json.invalid", json.loads(diagnostics.getvalue())["code"])

    def test_format_write_preserves_mode(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.sloph"
            path.write_text("module x;", encoding="ascii")
            path.chmod(0o755)
            self.assertEqual(0, main(["unstable", "format", str(path), "--write"]))
            self.assertEqual(0o755, stat.S_IMODE(path.stat().st_mode))
            self.assertEqual("module x;\n", path.read_text("ascii"))

    def test_jsonl_wraps_native_runtime_failure(self) -> None:
        completed = subprocess.CompletedProcess(
            ["program"], 2, stdout=b"partial", stderr=b"native failure\n"
        )
        result = BuildResult(Path("<temporary>"), "demo::main", {}, 0)
        diagnostics = io.StringIO()
        with patch("sloph.compiler.run_project", return_value=(result, completed)):
            with redirect_stderr(diagnostics):
                exit_code = main(
                    ["--diagnostics", "jsonl", "unstable", "run", "unused"]
                )
        self.assertEqual(1, exit_code)
        document = json.loads(diagnostics.getvalue())
        self.assertEqual("compiler.runtime.failed", document["code"])
        self.assertEqual(2, document["details"]["exit_code"])


class StableV1CliTests(unittest.TestCase):
    def test_check_does_not_run_package_build_scripts(self) -> None:
        with patch("sloph.compiler.build._run_package_build_scripts") as build_scripts:
            result = main(
                ["check", str(ROOT / "examples" / "hello-world")]
            )
        self.assertEqual(0, result)
        build_scripts.assert_not_called()

    def test_ast_json_identifies_version_one(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.sloph"
            path.write_text(
                "module demo; /* v1 */ fn answer() -> Int { 42 }",
                encoding="ascii",
            )
            output = io.StringIO()
            with patch("sys.stdout", output):
                result = main(["ast", "print", str(path)])
        self.assertEqual(0, result)
        document = json.loads(output.getvalue())
        self.assertEqual("sloph.syntax", document["schema"])
        self.assertEqual(1, document["version"])


if __name__ == "__main__":
    unittest.main()

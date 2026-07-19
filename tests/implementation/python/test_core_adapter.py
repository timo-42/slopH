from pathlib import Path
import contextlib
import io
import tempfile
import unittest

from sloph.cli import main
from sloph.core import DiagnosticError, Limits, format_core, parse_core


MINIMAL = b"(core 0 (types) (defs (def example::main Int (int 7))))"


class CoreAdapterLimitTests(unittest.TestCase):
    """Checks Python API limit plumbing that is not part of a shared CLI case."""

    def test_syntax_depth_limit_is_structured(self) -> None:
        with self.assertRaises(DiagnosticError) as raised:
            parse_core(b"(" * 5 + b")" * 5, Limits(syntax_depth=4))
        self.assertEqual("core.parse.limit_exceeded", raised.exception.diagnostic.code)

    def test_string_input_limit_precedes_encoding(self) -> None:
        with self.assertRaises(DiagnosticError) as raised:
            parse_core("x" * 5, Limits(input_bytes=4))
        self.assertEqual("core.parse.limit_exceeded", raised.exception.diagnostic.code)

    def test_core_output_limit_is_structured(self) -> None:
        with self.assertRaises(DiagnosticError) as raised:
            format_core(parse_core(MINIMAL), Limits(output_bytes=10))
        self.assertEqual("core.print.limit_exceeded", raised.exception.diagnostic.code)


class CoreCliAdapterTests(unittest.TestCase):
    """Checks host argument and filesystem adaptation rather than Core semantics."""

    def test_jsonl_usage_error(self) -> None:
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            result = main(
                [
                    "--diagnostics",
                    "jsonl",
                    "unstable",
                    "core",
                    "eval",
                    "input.core",
                    "--symbol",
                ]
            )
        self.assertEqual(2, result)
        self.assertIn('"code":"tool.usage"', errors.getvalue())

    def test_print_to_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.core"
            output = root / "output.core"
            source.write_bytes(MINIMAL)
            self.assertEqual(
                0,
                main(
                    [
                        "unstable",
                        "core",
                        "print",
                        str(source),
                        "-o",
                        str(output),
                    ]
                ),
            )
            self.assertEqual(format_core(parse_core(MINIMAL)), output.read_text("ascii"))


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import contextlib
import io
import tempfile
import unittest

from sloph.cli import main
from sloph.core import (
    DiagnosticError,
    Limits,
    evaluate,
    format_core,
    format_value,
    parse_core,
    validate,
)


MINIMAL = b"(core 0 (types) (defs (def example::main Int (int 7))))"


class CoreLibraryTests(unittest.TestCase):
    def test_canonical_print_is_idempotent(self) -> None:
        first = format_core(parse_core(MINIMAL))
        self.assertEqual(first, format_core(parse_core(first)))

    def test_exact_integer_arithmetic(self) -> None:
        source = b"""(core 0 (types) (defs
          (def example::main Int
            (prim int.mul (int 12345678901234567890) (int -9)))))"""
        value = evaluate(parse_core(source), "example::main")
        self.assertEqual("(value 0 (int -111111110111111111010))\n", format_value(value))

    def test_strict_constructor_and_case_evaluation(self) -> None:
        source = b"""(core 0
          (types (enum example::Pair
            (ctor example::Pair::Pair (field first Int) (field second Int))))
          (defs (def example::main Int
            (case (con example::Pair::Pair (int 4) (int 5)) Int
              (alt example::Pair::Pair (bind a Int) (bind b Int)
                (prim int.add (local a) (local b)))))))"""
        self.assertEqual(
            "(value 0 (int 9))\n",
            format_value(evaluate(parse_core(source), "example::main")),
        )

    def test_empty_enum_case_is_valid(self) -> None:
        source = b"""(core 0
          (types (enum example::Never))
          (defs (def example::absurd (fn (named example::Never) Int)
            (lam (bind impossible (named example::Never))
              (case (local impossible) Int)))))"""
        validate(parse_core(source))

    def test_cycle_under_lambda_is_rejected(self) -> None:
        source = b"""(core 0 (types) (defs
          (def example::loop (fn Int Int)
            (lam (bind x Int) (global example::loop)))))"""
        with self.assertRaises(DiagnosticError) as raised:
            validate(parse_core(source))
        self.assertEqual("core.validate.global_cycle", raised.exception.diagnostic.code)

    def test_function_constructor_field_is_rejected(self) -> None:
        source = b"""(core 0
          (types (enum example::Bad
            (ctor example::Bad::Bad (field callback (fn Int Int)))))
          (defs))"""
        with self.assertRaises(DiagnosticError) as raised:
            parse_core(source)
        self.assertEqual("core.parse.function_field", raised.exception.diagnostic.code)

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

    def test_cycle_diagnostic_excludes_acyclic_dependent(self) -> None:
        source = b"""(core 0 (types) (defs
          (def example::a Int (global example::b))
          (def example::b Int (global example::a))
          (def example::dependent Int (global example::a))))"""
        with self.assertRaises(DiagnosticError) as raised:
            validate(parse_core(source))
        self.assertEqual(
            ["example::a", "example::b"],
            raised.exception.diagnostic.details["definitions"],
        )

    def test_unknown_eval_symbol_is_structured(self) -> None:
        with self.assertRaises(DiagnosticError) as raised:
            evaluate(parse_core(MINIMAL), "example::missing")
        self.assertEqual("core.eval.unknown_symbol", raised.exception.diagnostic.code)


class CoreCliTests(unittest.TestCase):
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

import json
import unittest

from sloph.syntax import (
    CaseExpr, ConstructorExpr, DiagnosticError, Limits, LocalExpr,
    format_source, parse_source, syntax_from_json, syntax_to_json,
)


SOURCE = b"""module example::main;
import example::math::{number, Number};

public type Pair {
  Pair(first: Int, second: Int);
  Empty();
}

fn sum(pair: Pair) -> Int {
  let fallback: Int = 0;
  case pair -> Int {
    Pair::Pair(left: Int, right: Int) => {
      primitive int.add(left, right)
    }
    Pair::Empty() => {
      fallback
    }
  }
}

public value main: Int {
  sum(Pair::Pair(20, 22))
}
"""


class SourceSyntaxTests(unittest.TestCase):
    def test_full_profile_and_byte_spans(self) -> None:
        module = parse_source(SOURCE)
        self.assertEqual("example::main", module.name)
        self.assertEqual(("number", "Number"), module.imports[0].names)
        self.assertEqual(("Pair", "Empty"), tuple(c.name for c in module.types[0].constructors))
        case = module.functions[0].body.result
        self.assertIsInstance(case, CaseExpr)
        self.assertIsInstance(case.scrutinee, LocalExpr)
        constructor = module.values[0].value.result.arguments[0]
        self.assertIsInstance(constructor, ConstructorExpr)
        self.assertEqual(SOURCE.index(b"pair ->"), case.scrutinee.span.start)

    def test_typed_let_is_lexed_and_parsed(self) -> None:
        module = parse_source(SOURCE)
        binding = module.functions[0].body.bindings[0]
        self.assertEqual("fallback", binding.binder.name)
        self.assertEqual(0, binding.value.value)

    def test_canonical_format_is_idempotent(self) -> None:
        first = format_source(parse_source(SOURCE))
        self.assertEqual(first, format_source(parse_source(first)))

    def test_json_is_canonical_and_round_trips(self) -> None:
        module = parse_source(SOURCE)
        encoded = syntax_to_json(module)
        self.assertEqual(module, syntax_from_json(encoded))
        self.assertEqual(encoded, syntax_to_json(syntax_from_json(encoded)))
        self.assertEqual("sloph.syntax", json.loads(encoded)["schema"])
        self.assertEqual(0, json.loads(encoded)["version"])

    def test_json_rejects_unknown_fields(self) -> None:
        document = json.loads(syntax_to_json(parse_source(b"module x;")))
        document["extra"] = True
        with self.assertRaises(DiagnosticError) as raised:
            syntax_from_json(json.dumps(document))
        self.assertEqual("syntax.json.invalid", raised.exception.diagnostic.code)

    def test_json_string_input_is_bounded_before_encoding(self) -> None:
        with self.assertRaises(DiagnosticError) as raised:
            syntax_from_json("x" * 5, Limits(input_bytes=4))
        self.assertEqual("syntax.json.limit_exceeded", raised.exception.diagnostic.code)

    def test_json_huge_integer_is_a_structured_error(self) -> None:
        source = '{"schema":"sloph.syntax","version":' + "9" * 5000 + ',"module":{}}'
        with self.assertRaises(DiagnosticError) as raised:
            syntax_from_json(source)
        self.assertEqual("syntax.json.invalid", raised.exception.diagnostic.code)

    def test_json_and_in_memory_names_obey_token_limit(self) -> None:
        document = json.loads(syntax_to_json(parse_source(b"module x;")))
        document["module"]["name"] = "x" * 5
        with self.assertRaises(DiagnosticError) as raised:
            syntax_from_json(json.dumps(document), Limits(token_bytes=4))
        self.assertEqual("syntax.json.limit_exceeded", raised.exception.diagnostic.code)

    def test_no_comments(self) -> None:
        with self.assertRaises(DiagnosticError) as raised:
            parse_source(b"module x; // no")
        self.assertEqual("syntax.parse.invalid_character", raised.exception.diagnostic.code)

    def test_bare_carriage_return_is_rejected_but_crlf_is_allowed(self) -> None:
        self.assertEqual("x", parse_source(b"module x;\r\n").name)
        with self.assertRaises(DiagnosticError) as raised:
            parse_source(b"module x;\r")
        self.assertEqual("syntax.parse.bare_carriage_return", raised.exception.diagnostic.code)

    def test_declaration_casing_is_structured(self) -> None:
        for source in (b"module x; type bad {}", b"module x; fn Bad() -> Int { 0 }"):
            with self.subTest(source=source), self.assertRaises(DiagnosticError) as raised:
                parse_source(source)
            self.assertEqual("syntax.parse.invalid_name", raised.exception.diagnostic.code)

    def test_zero_parameter_and_dynamic_calls_are_structurally_rejected(self) -> None:
        for source in (
            b"module x; fn bad() -> Int { 0 }",
            b"module x; value main: Int { f() }",
            b"module x; value main: Int { f(1)(2) }",
        ):
            with self.subTest(source=source), self.assertRaises(DiagnosticError) as raised:
                parse_source(source)
            self.assertTrue(raised.exception.diagnostic.code.startswith("syntax.validate."))

    def test_in_memory_spans_are_bounded_before_json_encoding(self) -> None:
        from sloph.syntax import Module, Span

        module = Module("x", (), (), (), (), Span(0, 10**5000))
        with self.assertRaises(DiagnosticError) as raised:
            syntax_to_json(module)
        self.assertEqual("syntax.validate.limit_exceeded", raised.exception.diagnostic.code)

    def test_constructor_must_be_type_qualified(self) -> None:
        with self.assertRaises(DiagnosticError) as raised:
            parse_source(b"module x; value main: Int { Empty() }")
        self.assertEqual("syntax.parse.invalid_name", raised.exception.diagnostic.code)

    def test_limits_are_structured(self) -> None:
        with self.assertRaises(DiagnosticError) as raised:
            parse_source("x" * 5, Limits(input_bytes=4))
        self.assertEqual("syntax.parse.limit_exceeded", raised.exception.diagnostic.code)
        with self.assertRaises(DiagnosticError) as raised:
            format_source(parse_source(b"module x;"), Limits(output_bytes=4))
        self.assertEqual("syntax.print.limit_exceeded", raised.exception.diagnostic.code)

    def test_large_decimal_ignores_python_ambient_digit_cap(self) -> None:
        digits = "1" + "0" * 4499
        limits = Limits(input_bytes=10_000, token_bytes=5_000, literal_digits=5_000, output_bytes=10_000)
        module = parse_source(f"module x; value main: Int {{ {digits} }}", limits)
        self.assertIn(digits, format_source(module, limits))

    def test_json_ast_is_structurally_validated(self) -> None:
        document = json.loads(syntax_to_json(parse_source(b"module x;")))
        document["module"]["name"] = "Bad"
        with self.assertRaises(DiagnosticError) as raised:
            syntax_from_json(json.dumps(document))
        self.assertEqual("syntax.validate.invalid_name", raised.exception.diagnostic.code)


if __name__ == "__main__":
    unittest.main()

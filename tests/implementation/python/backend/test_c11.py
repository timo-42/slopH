from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest

from sloph.backend import emit_c, validate_profile
from sloph.core import DiagnosticError, Limits, parse_core
from sloph.core.canonical import decimal_string


CC = "/usr/bin/cc"


def run_c(source: bytes, symbol: str = "example::main") -> tuple[str, str]:
    unit = parse_core(source)
    generated = emit_c(unit, symbol)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        c_file = root / "unit.c"
        executable = root / "unit"
        c_file.write_text(generated, encoding="ascii")
        subprocess.run(
            [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", str(c_file), "-o", str(executable)],
            check=True,
            capture_output=True,
            text=True,
        )
        completed = subprocess.run(
            [str(executable)], check=True, capture_output=True, text=True
        )
    return generated, completed.stdout


class C11BackendTests(unittest.TestCase):
    def test_full_16384_bit_literal(self) -> None:
        integer = -(1 << 16_383)
        text = decimal_string(integer)
        source = f"(core 0 (types) (defs (def example::main Int (int {text}))))".encode()
        unit = parse_core(
            source,
            Limits(
                integer_bits=16_384,
                input_bytes=20_000,
                token_bytes=6_000,
                literal_digits=6_000,
            ),
        )
        generated = emit_c(unit, "example::main")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            c_file, executable = root / "unit.c", root / "unit"
            c_file.write_text(generated, encoding="ascii")
            subprocess.run(
                [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", str(c_file), "-o", str(executable)],
                check=True,
                capture_output=True,
                text=True,
            )
            output = subprocess.run(
                [str(executable)], check=True, capture_output=True, text=True
            ).stdout
        self.assertEqual(f"(value 0 (int {text}))\n", output)

    def test_exact_bigint_and_saturated_direct_call(self) -> None:
        source = b"""(core 0 (types) (defs
          (def example::combine (fn Int (fn Int Int))
            (lam (bind x Int) (lam (bind y Int)
              (prim int.sub
                (prim int.mul (local x) (local y))
                (int 99999999999999999999999999999999999999)))))
          (def example::main Int
            (app (app (global example::combine)
              (int 1234567890123456789012345678901234567890))
              (int -987654321098765432109876543210)))))"""
        generated, output = run_c(source)
        self.assertEqual(
            "(value 0 (int -1219326311370217952261850327337548559633622923332237463801111263526899))\n",
            output,
        )
        self.assertNotIn("Core", generated)
        self.assertEqual(generated, emit_c(parse_core(source), "example::main"))

    def test_constructor_case_and_lazy_data_global(self) -> None:
        source = b"""(core 0
          (types (enum example::Choice
            (ctor example::Choice::None)
            (ctor example::Choice::Some (field value Int))))
          (defs
            (def example::stored (named example::Choice)
              (con example::Choice::Some (int 41)))
            (def example::main Int
              (case (global example::stored) Int
                (alt example::Choice::None (int 0))
                (alt example::Choice::Some (bind x Int)
                  (prim int.add (local x) (int 1)))))))"""
        _, output = run_c(source)
        self.assertEqual("(value 0 (int 42))\n", output)

    def test_canonical_constructor_output(self) -> None:
        source = b"""(core 0
          (types (enum example::Tree
            (ctor example::Tree::Leaf (field value Int))
            (ctor example::Tree::Node
              (field left (named example::Tree))
              (field right (named example::Tree)))))
          (defs (def example::main (named example::Tree)
            (con example::Tree::Node
              (con example::Tree::Leaf (int -3))
              (con example::Tree::Leaf (int 8))))))"""
        _, output = run_c(source)
        self.assertEqual(
            "(value 0 (con example::Tree::Node (con example::Tree::Leaf (int -3)) (con example::Tree::Leaf (int 8))))\n",
            output,
        )

    def test_higher_order_profile_rejection_is_structured(self) -> None:
        source = b"""(core 0 (types) (defs
          (def example::apply (fn (fn Int Int) Int)
            (lam (bind f (fn Int Int)) (app (local f) (int 1))))
          (def example::main Int (int 0))))"""
        with self.assertRaises(DiagnosticError) as raised:
            validate_profile(parse_core(source), "example::main")
        self.assertEqual("backend.c11.higher_order_type", raised.exception.diagnostic.code)
        self.assertEqual("backend", raised.exception.diagnostic.phase)

    def test_function_entry_rejection_is_structured(self) -> None:
        source = b"""(core 0 (types) (defs
          (def example::identity (fn Int Int)
            (lam (bind x Int) (local x)))))"""
        with self.assertRaises(DiagnosticError) as raised:
            emit_c(parse_core(source), "example::identity")
        self.assertEqual("backend.c11.function_entry", raised.exception.diagnostic.code)

    def test_shared_value_output_amplification_is_bounded(self) -> None:
        definitions = [
            "(def example::v0 (named example::Pair) "
            "(con example::Pair::Leaf (int 0)))"
        ]
        for index in range(1, 19):
            definitions.append(
                f"(def example::v{index} (named example::Pair) "
                f"(con example::Pair::Pair (global example::v{index - 1}) "
                f"(global example::v{index - 1})))"
            )
        source = (
            "(core 0 (types (enum example::Pair "
            "(ctor example::Pair::Leaf (field value Int)) "
            "(ctor example::Pair::Pair "
            "(field left (named example::Pair)) "
            "(field right (named example::Pair))))) "
            f"(defs {' '.join(definitions)}))"
        ).encode()
        generated = emit_c(parse_core(source), "example::v18")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            c_file, executable = root / "unit.c", root / "unit"
            c_file.write_text(generated, encoding="ascii")
            subprocess.run(
                [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", str(c_file), "-o", str(executable)],
                check=True,
                capture_output=True,
            )
            completed = subprocess.run(
                [str(executable)], check=False, capture_output=True, timeout=5
            )
        self.assertEqual(2, completed.returncode)
        self.assertLessEqual(len(completed.stdout), 1_048_576)
        self.assertIn(b"output exceeds 1048576 bytes", completed.stderr)

    def test_allocation_free_call_dag_is_work_bounded(self) -> None:
        definitions = [
            "(def example::f0 (fn Int Int) (lam (bind x Int) (local x)))"
        ]
        for index in range(1, 25):
            definitions.append(
                f"(def example::f{index} (fn Int Int) (lam (bind x Int) "
                f"(let (bind dropped Int) "
                f"(app (global example::f{index - 1}) (local x)) "
                f"(app (global example::f{index - 1}) (local x)))))"
            )
        definitions.append(
            "(def example::main Int (app (global example::f24) (int 0)))"
        )
        source = f"(core 0 (types) (defs {' '.join(definitions)}))".encode()
        generated = emit_c(parse_core(source), "example::main")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            c_file, executable = root / "unit.c", root / "unit"
            c_file.write_text(generated, encoding="ascii")
            subprocess.run(
                [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", str(c_file), "-o", str(executable)],
                check=True,
                capture_output=True,
            )
            completed = subprocess.run(
                [str(executable)], check=False, capture_output=True, timeout=5
            )
        self.assertEqual(2, completed.returncode)
        self.assertIn(b"work limit exceeded (10000000)", completed.stderr)

    def test_repeated_wide_literal_conversion_is_work_bounded(self) -> None:
        literal = "1" + "0" * 4095
        definitions = [
            f"(def example::f0 (fn Int Int) (lam (bind x Int) (int {literal})))"
        ]
        for index in range(1, 5):
            definitions.append(
                f"(def example::f{index} (fn Int Int) (lam (bind x Int) "
                f"(let (bind dropped Int) "
                f"(app (global example::f{index - 1}) (local x)) "
                f"(app (global example::f{index - 1}) (local x)))))"
            )
        definitions.append(
            "(def example::main Int (app (global example::f4) (int 0)))"
        )
        source = f"(core 0 (types) (defs {' '.join(definitions)}))".encode()
        limits = Limits(input_bytes=20_000, token_bytes=5_000)
        generated = emit_c(parse_core(source, limits), "example::main")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            c_file, executable = root / "unit.c", root / "unit"
            c_file.write_text(generated, encoding="ascii")
            subprocess.run(
                [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", str(c_file), "-o", str(executable)],
                check=True,
                capture_output=True,
            )
            completed = subprocess.run(
                [str(executable)], check=False, capture_output=True, timeout=5
            )
        self.assertEqual(2, completed.returncode)
        self.assertIn(b"work limit exceeded (10000000)", completed.stderr)

    def test_parametric_core_is_erased_and_emitted_once(self) -> None:
        source = b"""(core 2 (types) (defs
          (def example::identity (forall T (fn (var T) (var T)))
            (lam (type-bind T) (lam (bind item (var T)) (local item))))
          (def example::main Int
            (let (bind ignored Bytes)
              (app (app (global example::identity) (type Bytes)) (bytes x78))
              (app (app (global example::identity) (type Int)) (int 42))))))"""
        generated, output = run_c(source)
        self.assertEqual("(value 0 (int 42))\n", output)
        self.assertEqual(1, generated.count("static SlValue *sl_f0(SlValue *a0) {"))
        self.assertNotIn("type-bind", generated)


if __name__ == "__main__":
    unittest.main()

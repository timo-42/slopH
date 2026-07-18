import random
import unittest

from sloph.core import DiagnosticError, format_core, parse_core


class ParserRobustnessTests(unittest.TestCase):
    def test_deterministic_malformed_byte_samples(self) -> None:
        randomizer = random.Random(0x51_0F)
        alphabet = b"() ;\n\r\tABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789:-_\x00\xff"
        for _ in range(1000):
            sample = bytes(randomizer.choice(alphabet) for _ in range(randomizer.randrange(0, 160)))
            try:
                unit = parse_core(sample)
                canonical = format_core(unit)
                self.assertEqual(canonical, format_core(parse_core(canonical)))
            except DiagnosticError:
                pass

    def test_deep_input_reports_diagnostic_not_recursion_error(self) -> None:
        sample = b"(" * 300 + b"x" + b")" * 300
        with self.assertRaises(DiagnosticError):
            parse_core(sample)


from pathlib import Path
import unittest

from tests.runners.python import discover, run


class SharedCoreCases(unittest.TestCase):
    def test_shared_cases(self) -> None:
        root = Path(__file__).parent / "core"
        cases = discover(root)
        self.assertGreater(len(cases), 0)
        for case in cases:
            with self.subTest(case=case.fields["name"]):
                outcome = run(case)
                self.assertEqual(int(case.fields["expect-exit"]), outcome.exit_code)
                expected_output = (
                    case.path("expect-output").read_text(encoding="utf-8")
                    if "expect-output" in case.fields
                    else ""
                )
                expected_diagnostics = (
                    case.path("expect-diagnostics").read_text(encoding="utf-8")
                    if "expect-diagnostics" in case.fields
                    else ""
                )
                self.assertEqual(expected_output, outcome.stdout)
                self.assertEqual(expected_diagnostics, outcome.stderr)

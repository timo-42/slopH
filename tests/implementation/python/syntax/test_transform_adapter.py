import unittest

from sloph.syntax import Capture, IfExpr, Literal, Transform, parse_source_v1


class PythonTransformAdapterTests(unittest.TestCase):
    """Checks Python callback adaptation, not portable SlopH behavior."""

    def test_custom_transform_uses_generic_typed_capture_pattern(self) -> None:
        def expand(captures, span):
            return IfExpr(
                captures["condition"],
                captures["yes"],
                captures["no"],
                span,
            )

        choose = Transform(
            "example::choose",
            "choose",
            (
                Capture("condition", "Expr"),
                Capture("yes", "Block"),
                Literal("otherwise"),
                Capture("no", "Block"),
            ),
            expand,
        )
        module = parse_source_v1(
            "module demo; const main: Int { choose 1 < 2 { 10 } otherwise { 20 } }",
            transforms=(choose,),
        )
        self.assertIsInstance(module.values[0].value.result, IfExpr)

    def test_transform_leading_name_conflicts_before_body_parsing(self) -> None:
        duplicate = Transform(
            "example::if",
            "if",
            (Capture("value", "Expr"),),
            lambda captures, span: captures["value"],
        )
        with self.assertRaisesRegex(Exception, "provided by both"):
            parse_source_v1(
                "module demo; const main: Int { 0 }",
                transforms=(duplicate,),
            )


if __name__ == "__main__":
    unittest.main()

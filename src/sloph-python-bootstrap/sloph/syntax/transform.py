from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Mapping, TypeAlias

from sloph.core.diagnostics import Span, fail
from sloph.syntax.model import Expr, IfExpr


@dataclass(frozen=True, slots=True)
class Capture:
    name: str
    category: str

    def __post_init__(self) -> None:
        if self.category not in {"Expr", "Block"}:
            raise ValueError(f"unsupported transform capture category {self.category!r}")


@dataclass(frozen=True, slots=True)
class Literal:
    text: str

    def __post_init__(self) -> None:
        if not self.text:
            raise ValueError("transform literals cannot be empty")


PatternPart: TypeAlias = Capture | Literal
Expansion: TypeAlias = Callable[[Mapping[str, object], Span], Expr]


@dataclass(frozen=True, slots=True)
class Transform:
    identity: str
    name: str
    pattern: tuple[PatternPart, ...]
    expand: Expansion

    def __post_init__(self) -> None:
        if not self.name or not (self.name[0].isalpha() or self.name[0] == "_"):
            raise ValueError("transform names must be identifiers")
        captures = [part.name for part in self.pattern if isinstance(part, Capture)]
        if len(captures) != len(set(captures)):
            raise ValueError("transform capture names must be unique")


class TransformRegistry:
    def __init__(self, transforms: tuple[Transform, ...] = ()) -> None:
        self._by_name: dict[str, Transform] = {}
        for transform in transforms:
            self.add(transform)

    def add(self, transform: Transform) -> None:
        previous = self._by_name.get(transform.name)
        if previous is not None:
            fail(
                "syntax.transform.name_conflict",
                "transform",
                f"transform name {transform.name!r} is provided by both "
                f"{previous.identity!r} and {transform.identity!r}",
                name=transform.name,
                first=previous.identity,
                second=transform.identity,
            )
        self._by_name[transform.name] = transform

    def get(self, name: str) -> Transform | None:
        return self._by_name.get(name)

    def extended(self, transforms: tuple[Transform, ...]) -> "TransformRegistry":
        result = TransformRegistry(tuple(self._by_name.values()))
        for transform in transforms:
            result.add(transform)
        return result


def _if(captures: Mapping[str, object], span: Span) -> Expr:
    return IfExpr(
        captures["condition"],
        captures["then"],
        captures["else"],
        span,
    )


def _unless(captures: Mapping[str, object], span: Span) -> Expr:
    return IfExpr(
        captures["condition"],
        captures["else"],
        captures["then"],
        span,
    )


STANDARD_TRANSFORMS = (
    Transform(
        "core::transform::if",
        "if",
        (
            Capture("condition", "Expr"),
            Capture("then", "Block"),
            Literal("else"),
            Capture("else", "Block"),
        ),
        _if,
    ),
    Transform(
        "core::transform::unless",
        "unless",
        (
            Capture("condition", "Expr"),
            Capture("then", "Block"),
            Literal("else"),
            Capture("else", "Block"),
        ),
        _unless,
    ),
)


def standard_transform_registry() -> TransformRegistry:
    return TransformRegistry(STANDARD_TRANSFORMS)


__all__ = [
    "Capture",
    "Literal",
    "PatternPart",
    "Transform",
    "TransformRegistry",
    "standard_transform_registry",
]

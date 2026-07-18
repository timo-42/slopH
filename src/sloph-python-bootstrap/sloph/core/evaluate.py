from __future__ import annotations

from dataclasses import dataclass
from typing import TypeAlias

from sloph.core.canonical import decimal_string
from sloph.core.diagnostics import (
    Diagnostic,
    DiagnosticError,
    Span,
    fail,
    limit_fail,
)
from sloph.core.limits import Limits
from sloph.core.model import (
    Alternative,
    AppExpr,
    Binder,
    BytesExpr,
    CaseExpr,
    ConExpr,
    CoreUnit,
    Expr,
    GlobalExpr,
    IntExpr,
    LamExpr,
    LetExpr,
    LocalExpr,
    PrimExpr,
)
from sloph.core.validate import validate


@dataclass(frozen=True, slots=True)
class IntValue:
    value: int


@dataclass(frozen=True, slots=True)
class BytesValue:
    value: bytes


@dataclass(frozen=True, slots=True)
class ConValue:
    constructor: str
    fields: tuple["Value", ...]
    depth: int


@dataclass(frozen=True, slots=True)
class Closure:
    binder: Binder
    body: Expr
    environment: dict[str, "Value"]


Value: TypeAlias = IntValue | BytesValue | ConValue | Closure


@dataclass(slots=True)
class _AppFunction:
    argument: Expr
    environment: dict[str, Value]


@dataclass(slots=True)
class _AppArgument:
    closure: Closure


@dataclass(slots=True)
class _LetValue:
    binder: Binder
    body: Expr
    environment: dict[str, Value]


@dataclass(slots=True)
class _PrimArguments:
    name: str
    arguments: tuple[Expr, ...]
    index: int
    values: list[Value]
    environment: dict[str, Value]
    span: Span


@dataclass(slots=True)
class _ConFields:
    constructor: str
    fields: tuple[Expr, ...]
    index: int
    values: list[Value]
    environment: dict[str, Value]
    span: Span


@dataclass(slots=True)
class _CaseScrutinee:
    alternatives: tuple[Alternative, ...]
    environment: dict[str, Value]
    span: Span


@dataclass(slots=True)
class _GlobalOutcome:
    name: str


Frame: TypeAlias = (
    _AppFunction
    | _AppArgument
    | _LetValue
    | _PrimArguments
    | _ConFields
    | _CaseScrutinee
    | _GlobalOutcome
)


class _Machine:
    def __init__(self, unit: CoreUnit, limits: Limits):
        self.unit = unit
        self.limits = limits
        self.definitions = {item.name: item for item in unit.definitions}
        self.cache: dict[str, Value | Diagnostic] = {}
        self.fuel = limits.fuel
        self.value_nodes = 0

    def run(self, symbol: str) -> Value:
        definition = self.definitions.get(symbol)
        if definition is None:
            fail(
                "core.eval.unknown_symbol",
                "eval",
                f"unknown evaluation symbol {symbol!r}",
                self.unit.span,
                symbol=symbol,
            )
        control: Expr | Value = GlobalExpr(symbol, definition.span)
        environment: dict[str, Value] = {}
        frames: list[Frame] = []
        try:
            while True:
                if isinstance(control, (IntValue, BytesValue, ConValue, Closure)):
                    if not frames:
                        return control
                    frame = frames.pop()
                    control, environment = self._resume(frame, control, frames)
                    continue

                expression = control
                self._consume(1, expression.span)
                if isinstance(expression, IntExpr):
                    self._check_integer(expression.value, expression.span)
                    control = self._int_value(expression.value, expression.span)
                elif isinstance(expression, BytesExpr):
                    self._consume(1 + len(expression.value), expression.span)
                    control = BytesValue(expression.value)
                elif isinstance(expression, LocalExpr):
                    control = environment[expression.name]
                elif isinstance(expression, GlobalExpr):
                    self._consume(1, expression.span)
                    cached = self.cache.get(expression.name)
                    if isinstance(cached, Diagnostic):
                        raise DiagnosticError(cached)
                    if cached is not None:
                        control = cached
                    else:
                        self._push(frames, _GlobalOutcome(expression.name), expression.span)
                        control = self.definitions[expression.name].value
                        environment = {}
                elif isinstance(expression, LamExpr):
                    control = self._closure(
                        expression.binder, expression.body, environment, expression.span
                    )
                elif isinstance(expression, AppExpr):
                    self._push(
                        frames,
                        _AppFunction(expression.argument, environment),
                        expression.span,
                    )
                    control = expression.function
                elif isinstance(expression, LetExpr):
                    self._push(
                        frames,
                        _LetValue(expression.binder, expression.body, environment),
                        expression.span,
                    )
                    control = expression.value
                elif isinstance(expression, PrimExpr):
                    frame = _PrimArguments(
                        expression.name,
                        expression.arguments,
                        0,
                        [],
                        environment,
                        expression.span,
                    )
                    self._push(frames, frame, expression.span)
                    control = expression.arguments[0]
                elif isinstance(expression, ConExpr):
                    if not expression.fields:
                        control = self._con_value(
                            expression.constructor, (), expression.span
                        )
                    else:
                        frame = _ConFields(
                            expression.constructor,
                            expression.fields,
                            0,
                            [],
                            environment,
                            expression.span,
                        )
                        self._push(frames, frame, expression.span)
                        control = expression.fields[0]
                elif isinstance(expression, CaseExpr):
                    self._push(
                        frames,
                        _CaseScrutinee(
                            expression.alternatives, environment, expression.span
                        ),
                        expression.span,
                    )
                    control = expression.scrutinee
                else:
                    fail(
                        "core.eval.expression_form",
                        "eval",
                        "unsupported expression representation",
                        expression.span,
                    )
        except DiagnosticError as error:
            # Every currently evaluating global receives the same first outcome.
            for frame in frames:
                if isinstance(frame, _GlobalOutcome):
                    self.cache.setdefault(frame.name, error.diagnostic)
            raise

    def _resume(
        self, frame: Frame, value: Value, frames: list[Frame]
    ) -> tuple[Expr | Value, dict[str, Value]]:
        if isinstance(frame, _GlobalOutcome):
            self.cache[frame.name] = value
            return value, {}
        if isinstance(frame, _AppFunction):
            if not isinstance(value, Closure):
                fail(
                    "core.eval.not_function",
                    "eval",
                    "application target did not evaluate to a closure",
                )
            self._push(frames, _AppArgument(value), value.body.span)
            return frame.argument, frame.environment
        if isinstance(frame, _AppArgument):
            environment = dict(frame.closure.environment)
            environment[frame.closure.binder.name] = value
            return frame.closure.body, environment
        if isinstance(frame, _LetValue):
            environment = dict(frame.environment)
            environment[frame.binder.name] = value
            return frame.body, environment
        if isinstance(frame, _PrimArguments):
            frame.values.append(value)
            frame.index += 1
            if frame.index < len(frame.arguments):
                self._push(frames, frame, frame.span)
                return frame.arguments[frame.index], frame.environment
            return self._primitive(frame.name, frame.values, frame.span), {}
        if isinstance(frame, _ConFields):
            frame.values.append(value)
            frame.index += 1
            if frame.index < len(frame.fields):
                self._push(frames, frame, frame.span)
                return frame.fields[frame.index], frame.environment
            return self._con_value(
                frame.constructor, tuple(frame.values), frame.span
            ), {}
        if isinstance(frame, _CaseScrutinee):
            if not isinstance(value, ConValue):
                fail(
                    "core.eval.case_value",
                    "eval",
                    "case scrutinee did not evaluate to constructor data",
                    frame.span,
                )
            alternative = next(
                item
                for item in frame.alternatives
                if item.constructor == value.constructor
            )
            environment = dict(frame.environment)
            for binder, field in zip(alternative.binders, value.fields):
                environment[binder.name] = field
            return alternative.body, environment
        raise AssertionError(f"unknown evaluator frame {type(frame)!r}")

    def _primitive(self, name: str, values: list[Value], span: Span) -> Value:
        if name.startswith("foreign.") or name == "runtime.trap":
            fail(
                "core.eval.effectful_primitive",
                "eval",
                f"the pure reference evaluator cannot execute {name}",
                span,
                primitive=name,
            )
        if name == "bytes.length":
            value = values[0]
            if not isinstance(value, BytesValue):
                fail("core.eval.primitive_value", "eval", "bytes.length received a non-Bytes value", span)
            self._consume(1, span)
            return IntValue(len(value.value))
        if name == "int.to_bytes":
            value = values[0]
            if not isinstance(value, IntValue):
                fail("core.eval.primitive_value", "eval", "int.to_bytes received a non-integer", span)
            rendered = decimal_string(value.value).encode("ascii")
            self._consume(1 + _limbs(value.value) + len(rendered), span)
            return BytesValue(rendered)
        left, right = values
        if not isinstance(left, IntValue) or not isinstance(right, IntValue):
            fail(
                "core.eval.primitive_value",
                "eval",
                f"primitive {name!r} received a non-integer",
                span,
            )
        left_limbs = _limbs(left.value)
        right_limbs = _limbs(right.value)
        if name in ("int.equal", "int.less"):
            self._consume(1 + left_limbs + right_limbs, span)
            truth = left.value == right.value if name == "int.equal" else left.value < right.value
            return ConValue(f"core::Bool::{'True' if truth else 'False'}", (), 1)
        if name == "int.mul":
            self._consume(1 + left_limbs * right_limbs, span)
            maximum_bits = _bits(left.value) + _bits(right.value)
            if maximum_bits > self.limits.integer_bits + 1:
                limit_fail("eval", "integer_bits", self.limits.integer_bits, span)
            result = left.value * right.value
        elif name == "int.add":
            self._consume(1 + left_limbs + right_limbs, span)
            result = left.value + right.value
        elif name == "int.sub":
            self._consume(1 + left_limbs + right_limbs, span)
            result = left.value - right.value
        else:
            fail(
                "core.eval.unknown_primitive",
                "eval",
                f"unknown primitive {name!r}",
                span,
            )
        self._check_integer(result, span)
        return self._int_value(result, span)

    def _consume(self, amount: int, span: Span) -> None:
        if amount > self.fuel:
            limit_fail("eval", "fuel", self.limits.fuel, span)
        self.fuel -= amount

    def _push(self, frames: list[Frame], frame: Frame, span: Span) -> None:
        if len(frames) + 1 > self.limits.evaluation_depth:
            limit_fail(
                "eval", "evaluation_depth", self.limits.evaluation_depth, span
            )
        frames.append(frame)

    def _allocate(self, span: Span) -> None:
        self.value_nodes += 1
        if self.value_nodes > self.limits.value_nodes:
            limit_fail("eval", "value_nodes", self.limits.value_nodes, span)

    def _int_value(self, value: int, span: Span) -> IntValue:
        self._allocate(span)
        return IntValue(value)

    def _closure(
        self,
        binder: Binder,
        body: Expr,
        environment: dict[str, Value],
        span: Span,
    ) -> Closure:
        self._allocate(span)
        return Closure(binder, body, dict(environment))

    def _con_value(
        self, constructor: str, fields: tuple[Value, ...], span: Span
    ) -> ConValue:
        depth = 1 + max((_value_depth(item) for item in fields), default=0)
        if depth > self.limits.evaluation_depth:
            limit_fail(
                "eval", "value_depth", self.limits.evaluation_depth, span
            )
        self._allocate(span)
        return ConValue(constructor, fields, depth)

    def _check_integer(self, value: int, span: Span) -> None:
        if _bits(value) > self.limits.integer_bits:
            limit_fail("eval", "integer_bits", self.limits.integer_bits, span)


def evaluate(
    unit: CoreUnit, symbol: str, limits: Limits | None = None
) -> Value:
    limits = limits or Limits()
    validate(unit)
    value = _Machine(unit, limits).run(symbol)
    if isinstance(value, Closure):
        fail(
            "core.eval.non_printable_result",
            "eval",
            "the selected symbol evaluates to a function",
            unit.span,
            symbol=symbol,
        )
    return value


def format_value(value: Value, limits: Limits | None = None) -> str:
    limits = limits or Limits()
    parts: list[str] = []
    size = 0
    nodes = 0
    stack: list[str | Value] = [")", value, "(value 0 "]

    def append(text: str) -> None:
        nonlocal size
        size += len(text.encode("ascii"))
        if size > limits.output_bytes:
            limit_fail("eval", "output_bytes", limits.output_bytes)
        parts.append(text)

    while stack:
        item = stack.pop()
        if isinstance(item, str):
            append(item)
            continue
        nodes += 1
        if nodes > limits.value_nodes:
            limit_fail("eval", "value_nodes", limits.value_nodes)
        if isinstance(item, IntValue):
            append("(int " + decimal_string(item.value) + ")")
        elif isinstance(item, BytesValue):
            append("(bytes x" + item.value.hex() + ")")
        elif isinstance(item, Closure):
            fail(
                "core.eval.non_printable_result",
                "eval",
                "function values are not printable Core v0 data",
            )
        elif isinstance(item, ConValue):
            stack.append(")")
            for field in reversed(item.fields):
                stack.append(field)
                stack.append(" ")
            stack.append("(con " + item.constructor)
        else:
            raise AssertionError(f"unsupported value {type(item)!r}")
    append("\n")
    return "".join(parts)


def _bits(value: int) -> int:
    return abs(value).bit_length()


def _limbs(value: int) -> int:
    return max(1, (_bits(value) + 63) // 64)


def _value_depth(value: Value) -> int:
    if isinstance(value, ConValue):
        return value.depth
    return 1

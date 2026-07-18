from __future__ import annotations

from collections import deque
import re

from sloph.core.diagnostics import Span, fail
from sloph.core.model import (
    INT,
    Alternative,
    AppExpr,
    BytesExpr,
    CaseExpr,
    ConExpr,
    ConstructorDecl,
    CoreType,
    CoreUnit,
    Definition,
    EnumDecl,
    Expr,
    FunctionType,
    GlobalExpr,
    IntExpr,
    IntType,
    LamExpr,
    LetExpr,
    LocalExpr,
    NamedType,
    PrimExpr,
)


SEGMENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
PRIMITIVES: dict[str, tuple[tuple[CoreType, ...], CoreType]] = {
    "int.add": ((INT, INT), INT),
    "int.sub": ((INT, INT), INT),
    "int.mul": ((INT, INT), INT),
}
V1_PRIMITIVES: dict[str, tuple[tuple[CoreType, ...], CoreType]] = PRIMITIVES | {
    "int.equal": ((INT, INT), NamedType("core::Bool")),
    "int.less": ((INT, INT), NamedType("core::Bool")),
    "io.write": ((NamedType("core::Bytes"),), NamedType("core::Unit")),
}


class _Context:
    def __init__(self, unit: CoreUnit):
        self.unit = unit
        self.enums: dict[str, EnumDecl] = {}
        self.constructors: dict[str, tuple[EnumDecl, ConstructorDecl]] = {}
        self.definitions: dict[str, Definition] = {}


def validate(unit: CoreUnit) -> None:
    if unit.version not in (0, 1):
        fail(
            "core.validate.unsupported_version",
            "validate",
            "only Core versions 0 and 1 are supported",
            unit.span,
            version=unit.version,
        )
    context = _Context(unit)
    _collect_declarations(context)
    _validate_declaration_types(context)
    _validate_global_cycles(context)
    for definition in sorted(unit.definitions, key=lambda item: item.name):
        _validate_definition(context, definition)


def _collect_declarations(context: _Context) -> None:
    for enum in context.unit.types:
        _global_id(enum.name, enum.span, "type")
        if enum.name in context.enums:
            _duplicate("type", enum.name, enum.span)
        context.enums[enum.name] = enum
        constructor_names: set[str] = set()
        for constructor in enum.constructors:
            _global_id(constructor.name, constructor.span, "constructor")
            expected_prefix = enum.name + "::"
            suffix = constructor.name[len(expected_prefix) :] if constructor.name.startswith(expected_prefix) else ""
            if not suffix or "::" in suffix or not SEGMENT_RE.fullmatch(suffix):
                fail(
                    "core.validate.constructor_identity",
                    "validate",
                    f"constructor {constructor.name!r} must be directly qualified by {enum.name!r}",
                    constructor.span,
                    enum=enum.name,
                    constructor=constructor.name,
                )
            if constructor.name in constructor_names or constructor.name in context.constructors:
                _duplicate("constructor", constructor.name, constructor.span)
            constructor_names.add(constructor.name)
            context.constructors[constructor.name] = (enum, constructor)
            field_names: set[str] = set()
            for field in constructor.fields:
                _local_id(field.name, field.span, "field")
                if field.name in field_names:
                    _duplicate("field", field.name, field.span)
                field_names.add(field.name)

    for definition in context.unit.definitions:
        _global_id(definition.name, definition.span, "definition")
        if definition.name in context.definitions:
            _duplicate("definition", definition.name, definition.span)
        context.definitions[definition.name] = definition


def _validate_declaration_types(context: _Context) -> None:
    for enum in sorted(context.unit.types, key=lambda item: item.name):
        for constructor in enum.constructors:
            for field in constructor.fields:
                if context.unit.version == 0 and isinstance(field.type, FunctionType):
                    fail(
                        "core.validate.function_field",
                        "validate",
                        "Core v0 nominal fields cannot have function type",
                        field.span,
                    )
                _well_formed_type(context, field.type, field.span)
    for definition in sorted(context.unit.definitions, key=lambda item: item.name):
        _well_formed_type(context, definition.type, definition.span)


def _well_formed_type(context: _Context, type_: CoreType, span: Span) -> None:
    pending = [type_]
    while pending:
        current = pending.pop()
        if isinstance(current, IntType):
            continue
        if isinstance(current, NamedType):
            _global_id(current.name, span, "type reference")
            if current.name not in context.enums:
                fail(
                    "core.validate.unknown_type",
                    "validate",
                    f"unknown nominal type {current.name!r}",
                    span,
                    type=current.name,
                )
            continue
        if isinstance(current, FunctionType):
            pending.append(current.result)
            pending.append(current.parameter)
            continue
        fail(
            "core.validate.type_form",
            "validate",
            "unknown Core type representation",
            span,
        )


def _validate_global_cycles(context: _Context) -> None:
    graph: dict[str, set[str]] = {}
    reverse: dict[str, set[str]] = {name: set() for name in context.definitions}
    for definition in context.unit.definitions:
        dependencies = _global_references(definition.value)
        for dependency, span in dependencies:
            if dependency not in context.definitions:
                fail(
                    "core.validate.unknown_global",
                    "validate",
                    f"unknown global definition {dependency!r}",
                    span,
                    global_id=dependency,
                )
        names = {name for name, _ in dependencies}
        # A v1 function definition evaluates to its closure without evaluating
        # the body. Recursive references in that body are therefore not a
        # value-initialization cycle. Data definitions remain acyclic.
        if context.unit.version == 1 and isinstance(definition.type, FunctionType):
            names = set()
        graph[definition.name] = names
        for name in names:
            reverse[name].add(definition.name)

    indegree = {name: len(dependencies) for name, dependencies in graph.items()}
    ready = deque(sorted(name for name, degree in indegree.items() if degree == 0))
    visited = 0
    while ready:
        name = ready.popleft()
        visited += 1
        for dependent in sorted(reverse[name]):
            indegree[dependent] -= 1
            if indegree[dependent] == 0:
                ready.append(dependent)
    if visited != len(graph):
        remaining = {name for name, degree in indegree.items() if degree > 0}
        cyclic = _deterministic_cycle(graph, remaining)
        first = context.definitions[cyclic[0]]
        fail(
            "core.validate.global_cycle",
            "validate",
            (
                "Core v0 value definitions must be acyclic"
                if context.unit.version == 0
                else "Core value definitions must be acyclic"
            ),
            first.span,
            definitions=cyclic,
        )


def _deterministic_cycle(
    graph: dict[str, set[str]], candidates: set[str]
) -> list[str]:
    state: dict[str, int] = {}
    parent: dict[str, str] = {}
    for start in sorted(candidates):
        if state.get(start, 0) != 0:
            continue
        state[start] = 1
        stack: list[tuple[str, object]] = [
            (start, iter(sorted(graph[start] & candidates)))
        ]
        while stack:
            current, edges = stack[-1]
            try:
                target = next(edges)
            except StopIteration:
                state[current] = 2
                stack.pop()
                continue
            target_state = state.get(target, 0)
            if target_state == 0:
                parent[target] = current
                state[target] = 1
                stack.append((target, iter(sorted(graph[target] & candidates))))
            elif target_state == 1:
                cycle = [target]
                cursor = current
                while cursor != target:
                    cycle.append(cursor)
                    cursor = parent[cursor]
                return sorted(cycle)
    raise AssertionError("Kahn remainder did not contain a cycle")


def _global_references(expression: Expr) -> list[tuple[str, Span]]:
    result: list[tuple[str, Span]] = []
    pending: list[Expr] = [expression]
    while pending:
        current = pending.pop()
        if isinstance(current, GlobalExpr):
            result.append((current.name, current.span))
        elif isinstance(current, LamExpr):
            pending.append(current.body)
        elif isinstance(current, AppExpr):
            pending.extend((current.argument, current.function))
        elif isinstance(current, LetExpr):
            pending.extend((current.body, current.value))
        elif isinstance(current, PrimExpr):
            pending.extend(reversed(current.arguments))
        elif isinstance(current, ConExpr):
            pending.extend(reversed(current.fields))
        elif isinstance(current, CaseExpr):
            pending.extend(reversed(tuple(alt.body for alt in current.alternatives)))
            pending.append(current.scrutinee)
    return result


def _validate_definition(context: _Context, definition: Definition) -> None:
    all_binders: set[str] = set()
    actual = _infer(context, definition.value, {}, all_binders)
    if actual != definition.type:
        fail(
            "core.validate.definition_type",
            "validate",
            f"definition {definition.name!r} has type {_type_text(actual)}, expected {_type_text(definition.type)}",
            definition.value.span,
            definition=definition.name,
            expected=_type_text(definition.type),
            actual=_type_text(actual),
        )


def _infer(
    context: _Context,
    expression: Expr,
    environment: dict[str, CoreType],
    all_binders: set[str],
) -> CoreType:
    if isinstance(expression, IntExpr):
        return INT
    if isinstance(expression, BytesExpr):
        if context.unit.version != 1:
            fail("core.validate.expression_form", "validate", "byte literals require Core version 1", expression.span)
        return NamedType("core::Bytes")
    if isinstance(expression, LocalExpr):
        if expression.name not in environment:
            fail(
                "core.validate.unbound_local",
                "validate",
                f"unbound local {expression.name!r}",
                expression.span,
                local=expression.name,
            )
        return environment[expression.name]
    if isinstance(expression, GlobalExpr):
        definition = context.definitions.get(expression.name)
        if definition is None:
            fail(
                "core.validate.unknown_global",
                "validate",
                f"unknown global definition {expression.name!r}",
                expression.span,
                global_id=expression.name,
            )
        return definition.type
    if isinstance(expression, LamExpr):
        _register_binder(context, expression.binder, all_binders)
        local_environment = dict(environment)
        local_environment[expression.binder.name] = expression.binder.type
        body_type = _infer(context, expression.body, local_environment, all_binders)
        return FunctionType(expression.binder.type, body_type)
    if isinstance(expression, AppExpr):
        function_type = _infer(context, expression.function, environment, all_binders)
        argument_type = _infer(context, expression.argument, environment, all_binders)
        if not isinstance(function_type, FunctionType):
            fail(
                "core.validate.not_function",
                "validate",
                f"application target has non-function type {_type_text(function_type)}",
                expression.function.span,
            )
        if argument_type != function_type.parameter:
            _type_mismatch(expression.argument.span, function_type.parameter, argument_type)
        return function_type.result
    if isinstance(expression, LetExpr):
        value_type = _infer(context, expression.value, environment, all_binders)
        _register_binder(context, expression.binder, all_binders)
        if value_type != expression.binder.type:
            _type_mismatch(expression.value.span, expression.binder.type, value_type)
        local_environment = dict(environment)
        local_environment[expression.binder.name] = expression.binder.type
        return _infer(context, expression.body, local_environment, all_binders)
    if isinstance(expression, PrimExpr):
        catalog = V1_PRIMITIVES if context.unit.version == 1 else PRIMITIVES
        signature = catalog.get(expression.name)
        if signature is None:
            fail(
                "core.validate.unknown_primitive",
                "validate",
                f"unknown primitive {expression.name!r}",
                expression.span,
                primitive=expression.name,
            )
        parameters, result = signature
        if len(expression.arguments) != len(parameters):
            fail(
                "core.validate.primitive_arity",
                "validate",
                f"primitive {expression.name!r} expects {len(parameters)} arguments",
                expression.span,
                expected=len(parameters),
                actual=len(expression.arguments),
            )
        for argument, parameter in zip(expression.arguments, parameters):
            actual = _infer(context, argument, environment, all_binders)
            if actual != parameter:
                _type_mismatch(argument.span, parameter, actual)
        return result
    if isinstance(expression, ConExpr):
        found = context.constructors.get(expression.constructor)
        if found is None:
            fail(
                "core.validate.unknown_constructor",
                "validate",
                f"unknown constructor {expression.constructor!r}",
                expression.span,
                constructor=expression.constructor,
            )
        enum, constructor = found
        if len(expression.fields) != len(constructor.fields):
            fail(
                "core.validate.constructor_arity",
                "validate",
                f"constructor {constructor.name!r} expects {len(constructor.fields)} fields",
                expression.span,
                expected=len(constructor.fields),
                actual=len(expression.fields),
            )
        for value, field in zip(expression.fields, constructor.fields):
            actual = _infer(context, value, environment, all_binders)
            if actual != field.type:
                _type_mismatch(value.span, field.type, actual)
        return NamedType(enum.name)
    if isinstance(expression, CaseExpr):
        return _infer_case(context, expression, environment, all_binders)
    fail(
        "core.validate.expression_form",
        "validate",
        "unknown Core expression representation",
        expression.span,
    )


def _infer_case(
    context: _Context,
    expression: CaseExpr,
    environment: dict[str, CoreType],
    all_binders: set[str],
) -> CoreType:
    scrutinee_type = _infer(context, expression.scrutinee, environment, all_binders)
    if not isinstance(scrutinee_type, NamedType):
        fail(
            "core.validate.case_scrutinee",
            "validate",
            "case scrutinee must have a named nominal type",
            expression.scrutinee.span,
            actual=_type_text(scrutinee_type),
        )
    _well_formed_type(context, expression.result_type, expression.span)
    enum = context.enums[scrutinee_type.name]
    provided: dict[str, Alternative] = {}
    for alternative in expression.alternatives:
        if alternative.constructor in provided:
            _duplicate("case alternative", alternative.constructor, alternative.span)
        provided[alternative.constructor] = alternative
    expected = {constructor.name for constructor in enum.constructors}
    actual = set(provided)
    if actual != expected:
        fail(
            "core.validate.case_exhaustiveness",
            "validate",
            f"case over {enum.name!r} must contain every constructor exactly once",
            expression.span,
            missing=sorted(expected - actual),
            foreign=sorted(actual - expected),
        )
    for constructor in enum.constructors:
        alternative = provided[constructor.name]
        if len(alternative.binders) != len(constructor.fields):
            fail(
                "core.validate.alternative_arity",
                "validate",
                f"alternative {constructor.name!r} expects {len(constructor.fields)} binders",
                alternative.span,
                expected=len(constructor.fields),
                actual=len(alternative.binders),
            )
        local_environment = dict(environment)
        for binder, field in zip(alternative.binders, constructor.fields):
            _register_binder(context, binder, all_binders)
            if binder.type != field.type:
                _type_mismatch(binder.span, field.type, binder.type)
            local_environment[binder.name] = binder.type
        body_type = _infer(context, alternative.body, local_environment, all_binders)
        if body_type != expression.result_type:
            _type_mismatch(
                alternative.body.span, expression.result_type, body_type
            )
    return expression.result_type


def _register_binder(context: _Context, binder, all_binders: set[str]) -> None:
    _local_id(binder.name, binder.span, "binder")
    _well_formed_type(context, binder.type, binder.span)
    if binder.name in all_binders:
        fail(
            "core.validate.duplicate_local",
            "validate",
            f"local binder {binder.name!r} is reused in one definition",
            binder.span,
            local=binder.name,
        )
    all_binders.add(binder.name)


def _global_id(value: str, span: Span, role: str) -> None:
    segments = value.split("::")
    if len(segments) < 2 or any(not SEGMENT_RE.fullmatch(item) for item in segments):
        fail(
            "core.validate.global_identity",
            "validate",
            f"{role} {value!r} is not a valid fully qualified identity",
            span,
            identity=value,
        )


def _local_id(value: str, span: Span, role: str) -> None:
    if not SEGMENT_RE.fullmatch(value):
        fail(
            "core.validate.local_identity",
            "validate",
            f"{role} {value!r} is not a valid local identity",
            span,
            identity=value,
        )


def _duplicate(role: str, name: str, span: Span) -> None:
    fail(
        f"core.validate.duplicate_{role.replace(' ', '_')}",
        "validate",
        f"duplicate {role} {name!r}",
        span,
        identity=name,
    )


def _type_mismatch(span: Span, expected: CoreType, actual: CoreType) -> None:
    fail(
        "core.validate.type_mismatch",
        "validate",
        f"expected {_type_text(expected)}, found {_type_text(actual)}",
        span,
        expected=_type_text(expected),
        actual=_type_text(actual),
    )


def _type_text(type_: CoreType) -> str:
    if isinstance(type_, IntType):
        return "Int"
    if isinstance(type_, NamedType):
        return f"(named {type_.name})"
    if isinstance(type_, FunctionType):
        return f"(fn {_type_text(type_.parameter)} {_type_text(type_.result)})"
    return "<invalid-type>"

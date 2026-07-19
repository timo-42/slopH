from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import re

from sloph.core.diagnostics import Span, fail
from sloph.core.model import (
    BYTES,
    INT,
    Alternative,
    AppliedType,
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
    ForAllType,
    GlobalExpr,
    IntExpr,
    IntType,
    BytesType,
    LamExpr,
    LetExpr,
    LocalExpr,
    NamedType,
    PrimExpr,
    TypeBinder,
    TypeExpr,
    TypeVariable,
)


SEGMENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
FOREIGN_RE = re.compile(r"foreign(?:\.[A-Za-z_][A-Za-z0-9_]*)+\Z")
PROVIDER_RE = re.compile(r"[a-z_][A-Za-z0-9_]*(?:::[a-z_][A-Za-z0-9_]*)+\Z")
HEADER_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_.-]*\Z")
from sloph.core.primitives import FIXED_PRIMITIVES, V0_PRIMITIVES


class _Context:
    def __init__(self, unit: CoreUnit):
        self.unit = unit
        self.enums: dict[str, EnumDecl] = {}
        self.constructors: dict[str, tuple[EnumDecl, ConstructorDecl]] = {}
        self.definitions: dict[str, Definition] = {}
        self.foreign_bindings = {
            item.identity: item
            for item in unit.foreign_bindings
            if item.adapter != "unavailable"
        }


def validate(unit: CoreUnit) -> None:
    if unit.version not in (0, 1, 2, 3):
        fail(
            "core.validate.unsupported_version",
            "validate",
            "only Core versions 0, 1, 2, and 3 are supported",
            unit.span,
            version=unit.version,
        )
    context = _Context(unit)
    if unit.version == 0 and unit.foreign_bindings:
        fail("core.validate.foreign_version", "validate", "foreign bindings require Core version 1 or later", unit.span)
    if len({item.identity for item in unit.foreign_bindings}) != len(unit.foreign_bindings):
        fail("core.validate.foreign_duplicate", "validate", "foreign binding identities must be unique", unit.span)
    _collect_declarations(context)
    _validate_declaration_types(context)
    for binding in unit.foreign_bindings:
        if not FOREIGN_RE.fullmatch(binding.identity):
            fail("core.validate.foreign_identity", "validate", f"invalid foreign binding identity {binding.identity!r}", unit.span)
        if not PROVIDER_RE.fullmatch(binding.provider):
            fail("core.validate.foreign_provider", "validate", f"invalid foreign provider identity {binding.provider!r}", unit.span)
        if not HEADER_RE.fullmatch(binding.header):
            fail("core.validate.foreign_header", "validate", f"invalid foreign header {binding.header!r}", unit.span)
        for parameter in binding.parameters:
            _well_formed_type(context, parameter, unit.span, set())
        _well_formed_type(context, binding.result, unit.span, set())
    _validate_global_cycles(context)
    for definition in sorted(unit.definitions, key=lambda item: item.name):
        _validate_definition(context, definition)
    if unit.version == 3:
        _validate_ownership(context)


def _collect_declarations(context: _Context) -> None:
    for enum in context.unit.types:
        _global_id(enum.name, enum.span, "type")
        if enum.name in context.enums:
            _duplicate("type", enum.name, enum.span)
        context.enums[enum.name] = enum
        if enum.type_parameters and context.unit.version < 2:
            fail("core.validate.generic_version", "validate", "generic declarations require Core version 2 or later", enum.span)
        if enum.owned and context.unit.version != 3:
            fail("core.validate.owned_version", "validate", "owned declarations require Core version 3", enum.span)
        if len(set(enum.type_parameters)) != len(enum.type_parameters):
            fail("core.validate.duplicate_type_parameter", "validate", "type parameters must be unique", enum.span, type=enum.name)
        for parameter in enum.type_parameters:
            _local_id(parameter, enum.span, "type parameter")
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
        type_variables = set(enum.type_parameters)
        for constructor in enum.constructors:
            for field in constructor.fields:
                if context.unit.version == 0 and isinstance(field.type, FunctionType):
                    fail(
                        "core.validate.function_field",
                        "validate",
                        "Core v0 nominal fields cannot have function type",
                        field.span,
                    )
                _well_formed_type(context, field.type, field.span, type_variables)
    for definition in sorted(context.unit.definitions, key=lambda item: item.name):
        _well_formed_type(context, definition.type, definition.span, set())


def _well_formed_type(context: _Context, type_: CoreType, span: Span, type_variables: set[str]) -> None:
    pending: list[tuple[CoreType, set[str]]] = [(type_, set(type_variables))]
    while pending:
        current, scope = pending.pop()
        if isinstance(current, BytesType) and context.unit.version == 0:
            fail("core.validate.bytes_version", "validate", "Bytes requires Core version 1 or later", span)
        if isinstance(current, (IntType, BytesType)):
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
            if context.enums[current.name].type_parameters:
                fail("core.validate.type_arity", "validate", f"generic type {current.name!r} requires type arguments", span, type=current.name, expected=len(context.enums[current.name].type_parameters), actual=0)
            continue
        if isinstance(current, TypeVariable):
            _local_id(current.name, span, "type variable")
            if current.name not in scope:
                fail("core.validate.free_type_variable", "validate", f"free type variable {current.name!r}", span, variable=current.name)
            continue
        if isinstance(current, AppliedType):
            _global_id(current.constructor, span, "type reference")
            enum = context.enums.get(current.constructor)
            if enum is None:
                fail("core.validate.unknown_type", "validate", f"unknown nominal type {current.constructor!r}", span, type=current.constructor)
            if len(current.arguments) != len(enum.type_parameters):
                fail("core.validate.type_arity", "validate", f"type {current.constructor!r} expects {len(enum.type_parameters)} arguments", span, type=current.constructor, expected=len(enum.type_parameters), actual=len(current.arguments))
            pending.extend((argument, set(scope)) for argument in current.arguments)
            continue
        if isinstance(current, FunctionType):
            if current.mode not in ("own", "borrow"):
                fail("core.validate.parameter_mode", "validate", "parameter mode must be own or borrow", span, mode=current.mode)
            if current.mode != "own" and context.unit.version != 3:
                fail("core.validate.parameter_mode_version", "validate", "borrow parameters require Core version 3", span, mode=current.mode)
            pending.append((current.result, set(scope)))
            pending.append((current.parameter, set(scope)))
            continue
        if isinstance(current, ForAllType):
            if context.unit.version < 2:
                fail("core.validate.generic_version", "validate", "universal types require Core version 2 or later", span)
            _local_id(current.parameter, span, "type parameter")
            if current.parameter in scope:
                fail("core.validate.duplicate_type_parameter", "validate", f"duplicate type parameter {current.parameter!r}", span)
            nested = set(scope)
            nested.add(current.parameter)
            pending.append((current.body, nested))
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
        if context.unit.version >= 1 and isinstance(_strip_forall(definition.type), FunctionType):
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
    actual = _infer(context, definition.value, {}, all_binders, set())
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
    type_variables: set[str],
) -> CoreType:
    if isinstance(expression, IntExpr):
        return INT
    if isinstance(expression, BytesExpr):
        if context.unit.version < 1:
            fail("core.validate.expression_form", "validate", "byte literals require Core version 1 or later", expression.span)
        return BYTES
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
    if isinstance(expression, TypeExpr):
        fail("core.validate.type_expression_position", "validate", "type arguments are only valid as application arguments", expression.span)
    if isinstance(expression, LamExpr):
        if isinstance(expression.binder, TypeBinder):
            if context.unit.version < 2:
                fail("core.validate.generic_version", "validate", "type abstractions require Core version 2 or later", expression.span)
            _local_id(expression.binder.name, expression.binder.span, "type binder")
            if expression.binder.name in type_variables:
                fail("core.validate.duplicate_type_parameter", "validate", f"duplicate type binder {expression.binder.name!r}", expression.binder.span)
            nested_types = set(type_variables)
            nested_types.add(expression.binder.name)
            body_type = _infer(context, expression.body, environment, all_binders, nested_types)
            return ForAllType(expression.binder.name, body_type)
        if expression.binder.mode not in ("own", "borrow"):
            fail("core.validate.parameter_mode", "validate", "binder mode must be own or borrow", expression.binder.span, mode=expression.binder.mode)
        if expression.binder.mode != "own" and context.unit.version != 3:
            fail("core.validate.parameter_mode_version", "validate", "borrow binders require Core version 3", expression.binder.span, mode=expression.binder.mode)
        _register_binder(context, expression.binder, all_binders, type_variables)
        local_environment = dict(environment)
        local_environment[expression.binder.name] = expression.binder.type
        body_type = _infer(context, expression.body, local_environment, all_binders, type_variables)
        return FunctionType(expression.binder.type, body_type, expression.binder.mode)
    if isinstance(expression, AppExpr):
        function_type = _infer(context, expression.function, environment, all_binders, type_variables)
        if isinstance(expression.argument, TypeExpr):
            if not isinstance(function_type, ForAllType):
                fail("core.validate.not_polymorphic", "validate", "type application target is not polymorphic", expression.function.span)
            _well_formed_type(context, expression.argument.type, expression.argument.span, type_variables)
            return _substitute(function_type.body, {function_type.parameter: expression.argument.type})
        argument_type = _infer(context, expression.argument, environment, all_binders, type_variables)
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
        value_type = _infer(context, expression.value, environment, all_binders, type_variables)
        _register_binder(context, expression.binder, all_binders, type_variables)
        if value_type != expression.binder.type:
            _type_mismatch(expression.value.span, expression.binder.type, value_type)
        local_environment = dict(environment)
        local_environment[expression.binder.name] = expression.binder.type
        return _infer(context, expression.body, local_environment, all_binders, type_variables)
    if isinstance(expression, PrimExpr):
        catalog = FIXED_PRIMITIVES if context.unit.version >= 1 else V0_PRIMITIVES
        signature = catalog.get(expression.name)
        if signature is None and context.unit.version >= 1:
            binding = context.foreign_bindings.get(expression.name)
            if binding is not None:
                signature = (binding.parameters, binding.result)
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
            actual = _infer(context, argument, environment, all_binders, type_variables)
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
        if len(expression.type_arguments) != len(enum.type_parameters):
            fail("core.validate.constructor_type_arity", "validate", f"constructor {constructor.name!r} expects {len(enum.type_parameters)} type arguments", expression.span, expected=len(enum.type_parameters), actual=len(expression.type_arguments))
        for argument in expression.type_arguments:
            _well_formed_type(context, argument, expression.span, type_variables)
        substitutions = dict(zip(enum.type_parameters, expression.type_arguments, strict=True))
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
            actual = _infer(context, value, environment, all_binders, type_variables)
            expected = _substitute(field.type, substitutions)
            if actual != expected:
                _type_mismatch(value.span, expected, actual)
        return AppliedType(enum.name, expression.type_arguments) if enum.type_parameters else NamedType(enum.name)
    if isinstance(expression, CaseExpr):
        return _infer_case(context, expression, environment, all_binders, type_variables)
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
    type_variables: set[str],
) -> CoreType:
    scrutinee_type = _infer(context, expression.scrutinee, environment, all_binders, type_variables)
    if not isinstance(scrutinee_type, (NamedType, AppliedType)):
        fail(
            "core.validate.case_scrutinee",
            "validate",
            "case scrutinee must have a named nominal type",
            expression.scrutinee.span,
            actual=_type_text(scrutinee_type),
        )
    _well_formed_type(context, expression.result_type, expression.span, type_variables)
    enum_name = scrutinee_type.name if isinstance(scrutinee_type, NamedType) else scrutinee_type.constructor
    enum = context.enums[enum_name]
    arguments = () if isinstance(scrutinee_type, NamedType) else scrutinee_type.arguments
    substitutions = dict(zip(enum.type_parameters, arguments, strict=True))
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
            _register_binder(context, binder, all_binders, type_variables)
            expected_field = _substitute(field.type, substitutions)
            if binder.type != expected_field:
                _type_mismatch(binder.span, expected_field, binder.type)
            local_environment[binder.name] = binder.type
        body_type = _infer(context, alternative.body, local_environment, all_binders, type_variables)
        if body_type != expression.result_type:
            _type_mismatch(
                alternative.body.span, expression.result_type, body_type
            )
    return expression.result_type


def _register_binder(context: _Context, binder, all_binders: set[str], type_variables: set[str]) -> None:
    _local_id(binder.name, binder.span, "binder")
    _well_formed_type(context, binder.type, binder.span, type_variables)
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
    if isinstance(type_, BytesType):
        return "Bytes"
    if isinstance(type_, NamedType):
        return f"(named {type_.name})"
    if isinstance(type_, TypeVariable):
        return f"(var {type_.name})"
    if isinstance(type_, AppliedType):
        return f"(apply {type_.constructor} {' '.join(_type_text(item) for item in type_.arguments)})"
    if isinstance(type_, FunctionType):
        mode = "" if type_.mode == "own" else f"{type_.mode} "
        return f"(fn {mode}{_type_text(type_.parameter)} {_type_text(type_.result)})"
    if isinstance(type_, ForAllType):
        return f"(forall {type_.parameter} {_type_text(type_.body)})"
    return "<invalid-type>"


def _substitute(type_: CoreType, substitutions: dict[str, CoreType]) -> CoreType:
    if isinstance(type_, TypeVariable):
        return substitutions.get(type_.name, type_)
    if isinstance(type_, AppliedType):
        return AppliedType(type_.constructor, tuple(_substitute(item, substitutions) for item in type_.arguments))
    if isinstance(type_, FunctionType):
        return FunctionType(_substitute(type_.parameter, substitutions), _substitute(type_.result, substitutions), type_.mode)
    if isinstance(type_, ForAllType):
        nested = dict(substitutions)
        nested.pop(type_.parameter, None)
        return ForAllType(type_.parameter, _substitute(type_.body, nested))
    return type_


def _strip_forall(type_: CoreType) -> CoreType:
    while isinstance(type_, ForAllType):
        type_ = type_.body
    return type_


@dataclass(slots=True)
class _OwnershipLocal:
    type: CoreType
    permission: str
    moved: bool = False


def _validate_ownership(context: _Context) -> None:
    for definition in sorted(context.unit.definitions, key=lambda item: item.name):
        if not isinstance(_strip_forall(definition.type), FunctionType) and not _is_copy_type(context, definition.type):
            fail(
                "core.validate.owned_global",
                "validate",
                "owned values cannot be stored in process-lifetime globals",
                definition.span,
                definition=definition.name,
            )
        _check_owned_expr(context, definition.value, {}, {}, "own")


def _is_copy_type(
    context: _Context,
    type_: CoreType,
    substitutions: dict[str, CoreType] | None = None,
    seen: set[tuple[str, tuple[CoreType, ...]]] | None = None,
) -> bool:
    substitutions = substitutions or {}
    seen = seen or set()
    if isinstance(type_, (IntType, BytesType, FunctionType, ForAllType)):
        return True
    if isinstance(type_, TypeVariable):
        replacement = substitutions.get(type_.name)
        return replacement is not None and _is_copy_type(context, replacement, substitutions, seen)
    if isinstance(type_, NamedType):
        enum_name, arguments = type_.name, ()
    elif isinstance(type_, AppliedType):
        enum_name, arguments = type_.constructor, type_.arguments
    else:
        return True
    enum = context.enums[enum_name]
    if enum.owned:
        return False
    key = (enum_name, arguments)
    if key in seen:
        return True
    nested_seen = set(seen)
    nested_seen.add(key)
    nested = dict(substitutions)
    nested.update(zip(enum.type_parameters, arguments))
    return all(
        _is_copy_type(context, field.type, nested, nested_seen)
        for constructor in enum.constructors
        for field in constructor.fields
    )


def _ownership_type(
    context: _Context, expression: Expr, environment: dict[str, CoreType]
) -> CoreType:
    if isinstance(expression, IntExpr):
        return INT
    if isinstance(expression, BytesExpr):
        return BYTES
    if isinstance(expression, LocalExpr):
        return environment[expression.name]
    if isinstance(expression, GlobalExpr):
        return context.definitions[expression.name].type
    if isinstance(expression, TypeExpr):
        return expression.type
    if isinstance(expression, LamExpr):
        if isinstance(expression.binder, TypeBinder):
            return ForAllType(expression.binder.name, _ownership_type(context, expression.body, environment))
        nested = dict(environment)
        nested[expression.binder.name] = expression.binder.type
        return FunctionType(expression.binder.type, _ownership_type(context, expression.body, nested), expression.binder.mode)
    if isinstance(expression, AppExpr):
        function = _ownership_type(context, expression.function, environment)
        if isinstance(expression.argument, TypeExpr):
            assert isinstance(function, ForAllType)
            return _substitute(function.body, {function.parameter: expression.argument.type})
        assert isinstance(function, FunctionType)
        return function.result
    if isinstance(expression, LetExpr):
        nested = dict(environment)
        nested[expression.binder.name] = expression.binder.type
        return _ownership_type(context, expression.body, nested)
    if isinstance(expression, PrimExpr):
        signature = FIXED_PRIMITIVES.get(expression.name)
        if signature is None:
            binding = context.foreign_bindings[expression.name]
            return binding.result
        return signature[1]
    if isinstance(expression, ConExpr):
        enum, _ = context.constructors[expression.constructor]
        return AppliedType(enum.name, expression.type_arguments) if enum.type_parameters else NamedType(enum.name)
    if isinstance(expression, CaseExpr):
        return expression.result_type
    raise AssertionError(type(expression))


def _clone_ownership(state: dict[str, _OwnershipLocal]) -> dict[str, _OwnershipLocal]:
    return {
        name: _OwnershipLocal(item.type, item.permission, item.moved)
        for name, item in state.items()
    }


def _use_owned_local(
    context: _Context,
    expression: LocalExpr,
    state: dict[str, _OwnershipLocal],
    usage: str,
) -> None:
    item = state.get(expression.name)
    if item is None or _is_copy_type(context, item.type):
        return
    if item.moved:
        fail(
            "core.validate.use_after_move",
            "validate",
            f"owned local {expression.name!r} is used after it was moved",
            expression.span,
            local=expression.name,
        )
    if usage == "own":
        if item.permission == "borrow":
            fail(
                "core.validate.move_borrow",
                "validate",
                f"borrowed local {expression.name!r} cannot be moved",
                expression.span,
                local=expression.name,
            )
        item.moved = True


def _check_owned_expr(
    context: _Context,
    expression: Expr,
    environment: dict[str, CoreType],
    state: dict[str, _OwnershipLocal],
    usage: str,
) -> CoreType:
    if isinstance(expression, (IntExpr, BytesExpr, GlobalExpr)):
        return _ownership_type(context, expression, environment)
    if isinstance(expression, LocalExpr):
        _use_owned_local(context, expression, state, usage)
        return environment[expression.name]
    if isinstance(expression, TypeExpr):
        return expression.type
    if isinstance(expression, LamExpr):
        if isinstance(expression.binder, TypeBinder):
            body = _check_owned_expr(context, expression.body, environment, state, "own")
            return ForAllType(expression.binder.name, body)
        nested_environment = dict(environment)
        nested_environment[expression.binder.name] = expression.binder.type
        nested_state = _clone_ownership(state)
        if not _is_copy_type(context, expression.binder.type):
            nested_state[expression.binder.name] = _OwnershipLocal(
                expression.binder.type, expression.binder.mode
            )
        body = _check_owned_expr(
            context, expression.body, nested_environment, nested_state, "own"
        )
        item = nested_state.get(expression.binder.name)
        if item is not None and item.permission == "own" and not item.moved:
            fail(
                "core.validate.owned_not_consumed",
                "validate",
                f"owned parameter {expression.binder.name!r} is not consumed",
                expression.binder.span,
                local=expression.binder.name,
            )
        return FunctionType(expression.binder.type, body, expression.binder.mode)
    if isinstance(expression, AppExpr):
        function_type = _check_owned_expr(
            context, expression.function, environment, state, "borrow"
        )
        if isinstance(expression.argument, TypeExpr):
            assert isinstance(function_type, ForAllType)
            return _substitute(
                function_type.body, {function_type.parameter: expression.argument.type}
            )
        assert isinstance(function_type, FunctionType)
        _check_owned_expr(
            context, expression.argument, environment, state, function_type.mode
        )
        return function_type.result
    if isinstance(expression, LetExpr):
        if expression.binder.mode != "own":
            fail(
                "core.validate.let_parameter_mode",
                "validate",
                "let binders cannot declare a borrow mode",
                expression.binder.span,
            )
        _check_owned_expr(context, expression.value, environment, state, "own")
        nested_environment = dict(environment)
        nested_environment[expression.binder.name] = expression.binder.type
        nested_state = _clone_ownership(state)
        if not _is_copy_type(context, expression.binder.type):
            nested_state[expression.binder.name] = _OwnershipLocal(
                expression.binder.type, "own"
            )
        result = _check_owned_expr(
            context, expression.body, nested_environment, nested_state, usage
        )
        item = nested_state.get(expression.binder.name)
        if item is not None and not item.moved:
            fail(
                "core.validate.owned_not_consumed",
                "validate",
                f"owned local {expression.binder.name!r} must be moved or explicitly dropped",
                expression.binder.span,
                local=expression.binder.name,
            )
        for name in state:
            state[name].moved = nested_state[name].moved
        return result
    if isinstance(expression, PrimExpr):
        signature = FIXED_PRIMITIVES.get(expression.name)
        if signature is None:
            binding = context.foreign_bindings[expression.name]
            parameters, result = binding.parameters, binding.result
        else:
            parameters, result = signature
        for argument, parameter in zip(expression.arguments, parameters):
            argument_usage = "borrow" if _is_copy_type(context, parameter) else "own"
            _check_owned_expr(context, argument, environment, state, argument_usage)
        return result
    if isinstance(expression, ConExpr):
        enum, constructor = context.constructors[expression.constructor]
        substitutions = dict(zip(enum.type_parameters, expression.type_arguments))
        for value, field in zip(expression.fields, constructor.fields):
            field_type = _substitute(field.type, substitutions)
            field_usage = "borrow" if _is_copy_type(context, field_type) else "own"
            _check_owned_expr(context, value, environment, state, field_usage)
        return AppliedType(enum.name, expression.type_arguments) if enum.type_parameters else NamedType(enum.name)
    if isinstance(expression, CaseExpr):
        scrutinee_type = _ownership_type(context, expression.scrutinee, environment)
        scrutinee_usage = "borrow"
        if not _is_copy_type(context, scrutinee_type):
            if isinstance(expression.scrutinee, LocalExpr):
                local = state.get(expression.scrutinee.name)
                scrutinee_usage = "borrow" if local is not None and local.permission == "borrow" else "own"
            else:
                scrutinee_usage = "own"
        _check_owned_expr(
            context, expression.scrutinee, environment, state, scrutinee_usage
        )
        enum_name = scrutinee_type.name if isinstance(scrutinee_type, NamedType) else scrutinee_type.constructor
        enum = context.enums[enum_name]
        arguments = () if isinstance(scrutinee_type, NamedType) else scrutinee_type.arguments
        substitutions = dict(zip(enum.type_parameters, arguments))
        branch_states: list[dict[str, _OwnershipLocal]] = []
        alternatives = {item.constructor: item for item in expression.alternatives}
        for constructor in enum.constructors:
            alternative = alternatives[constructor.name]
            branch_environment = dict(environment)
            branch_state = _clone_ownership(state)
            owned_binders: list[str] = []
            for binder, field in zip(alternative.binders, constructor.fields):
                field_type = _substitute(field.type, substitutions)
                branch_environment[binder.name] = field_type
                if not _is_copy_type(context, field_type):
                    permission = "borrow" if scrutinee_usage == "borrow" else "own"
                    branch_state[binder.name] = _OwnershipLocal(field_type, permission)
                    if permission == "own":
                        owned_binders.append(binder.name)
            _check_owned_expr(
                context,
                alternative.body,
                branch_environment,
                branch_state,
                usage,
            )
            for name in owned_binders:
                if not branch_state[name].moved:
                    fail(
                        "core.validate.owned_not_consumed",
                        "validate",
                        f"owned pattern field {name!r} must be moved or explicitly dropped",
                        alternative.span,
                        local=name,
                    )
            for binder in alternative.binders:
                branch_state.pop(binder.name, None)
            branch_states.append(branch_state)
        if not branch_states:
            return expression.result_type
        baseline = {name: item.moved for name, item in branch_states[0].items()}
        for branch in branch_states[1:]:
            actual = {name: item.moved for name, item in branch.items()}
            if actual != baseline:
                fail(
                    "core.validate.ownership_branch",
                    "validate",
                    "case branches must leave the same ownership state",
                    expression.span,
                )
        for name, moved in baseline.items():
            state[name].moved = moved
        return expression.result_type
    raise AssertionError(type(expression))

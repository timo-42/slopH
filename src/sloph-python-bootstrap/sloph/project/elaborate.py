from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

from sloph.core.diagnostics import Span, UNKNOWN_SPAN, fail
from sloph.core.model import (
    INT,
    Alternative,
    AppliedType,
    AppExpr,
    Binder,
    BytesExpr,
    CaseExpr,
    ConExpr,
    ConstructorDecl,
    CoreType,
    CoreUnit,
    Definition,
    EnumDecl,
    FieldDecl,
    FunctionType,
    ForAllType,
    GlobalExpr,
    IntExpr,
    LamExpr,
    LetExpr,
    LocalExpr,
    NamedType,
    PrimExpr,
    TypeBinder,
    TypeExpr,
    TypeVariable,
)
from sloph.core.validate import validate
from sloph.core.limits import Limits
from sloph.project.load import load_project
from sloph.project.model import Project, ProjectModule


@dataclass(frozen=True, slots=True)
class _Symbol:
    name: str
    global_name: str
    kind: str
    declaration: Any
    module: str
    public: bool


@dataclass(slots=True)
class _Scope:
    module: ProjectModule
    own: dict[str, _Symbol]
    visible: dict[str, _Symbol]
    constructors: dict[str, tuple[str, Any]]
    version: int
    foreign_bindings: dict[str, Any]
    core_types: dict[str, _Symbol]
    scopes: dict[str, "_Scope"]


def elaborate_project(
    path: str | Path | Project, limits: Limits | None = None
) -> CoreUnit:
    """Resolve and lower the complete project to a validated Core v0 unit."""

    project = path if isinstance(path, Project) else load_project(path, limits)
    return _elaborate(project, version=0)


def elaborate_project_v1(
    path: str | Path | Project, limits: Limits | None = None
) -> CoreUnit:
    """Resolve Source v1 into independently validated parametric Core v2."""
    project = path if isinstance(path, Project) else load_project(
        path, limits, source_version=1
    )
    return _elaborate(project, version=2)


def _elaborate(project: Project, *, version: int) -> CoreUnit:
    scopes = _build_scopes(project, version=version)
    enums: list[EnumDecl] = []
    if version >= 1:
        enums.extend(
            (
                EnumDecl(
                    "core::Bytes",
                    (),
                ),
            )
        )
    definitions: list[Definition] = []
    for module in project.modules:
        scope = scopes[module.name]
        for declaration in _sequence(module.syntax, "types"):
            enums.append(_lower_enum(scope, declaration))
        for declaration in _sequence(module.syntax, "functions"):
            definitions.append(_lower_function(scope, declaration, version=version))
        for declaration in _sequence(module.syntax, "values"):
            definitions.append(_lower_value(scope, declaration))
    unit = CoreUnit(
        version,
        tuple(sorted(enums, key=lambda item: item.name)),
        tuple(sorted(definitions, key=lambda item: item.name)),
        foreign_bindings=project.foreign_bindings,
    )
    validate(unit)
    _validate_entry(project, unit)
    return unit


def _build_scopes(project: Project, *, version: int = 0) -> dict[str, _Scope]:
    scopes: dict[str, _Scope] = {}
    foreign_bindings = {binding.identity: binding for binding in project.foreign_bindings}
    for module in project.modules:
        own: dict[str, _Symbol] = {}
        constructors: dict[str, tuple[str, Any]] = {}
        groups = (
            ("type", _sequence(module.syntax, "types")),
            ("function", _sequence(module.syntax, "functions")),
            ("value", _sequence(module.syntax, "values")),
        )
        for kind, declarations in groups:
            for declaration in declarations:
                name = declaration.name
                if name in own:
                    fail(
                        "project.resolve.declaration_collision",
                        "resolve",
                        f"{name!r} is declared more than once in module {module.name!r}",
                        _span(declaration),
                        module=module.name,
                        name=name,
                    )
                own[name] = _Symbol(
                    name,
                    f"{module.name}::{name}",
                    kind,
                    declaration,
                    module.name,
                    bool(getattr(declaration, "public", False)),
                )
                if kind == "type":
                    seen: set[str] = set()
                    for constructor in _sequence(declaration, "constructors"):
                        if constructor.name in seen:
                            fail(
                                "project.resolve.constructor_collision",
                                "resolve",
                                f"constructor {constructor.name!r} is declared more than once",
                                _span(constructor),
                                type=name,
                            )
                        seen.add(constructor.name)
                        constructors[f"{name}::{constructor.name}"] = (
                            f"{module.name}::{name}::{constructor.name}",
                            constructor,
                        )
        scopes[module.name] = _Scope(
            module, own, dict(own), constructors, version, foreign_bindings, {}, {}
        )

    core_scope = scopes.get("core")
    for scope in scopes.values():
        scope.scopes = scopes
    if core_scope is not None:
        core_types = {
            name: symbol for name, symbol in core_scope.own.items() if symbol.kind == "type"
        }
        for scope in scopes.values():
            scope.core_types = core_types
            for name, symbol in core_types.items():
                declaration = symbol.declaration
                for constructor in _sequence(declaration, "constructors"):
                    value = (f"core::{name}::{constructor.name}", constructor)
                    scope.constructors.setdefault(f"{name}::{constructor.name}", value)
                    scope.constructors.setdefault(f"core::{name}::{constructor.name}", value)

    for module in project.modules:
        scope = scopes[module.name]
        for import_decl in _sequence(module.syntax, "imports"):
            imported_module = _import_module(import_decl)
            target = scopes[imported_module]
            for name in _sequence(import_decl, "names"):
                symbol = target.own.get(name)
                if symbol is None:
                    fail(
                        "project.resolve.unknown_import",
                        "resolve",
                        f"module {imported_module!r} has no declaration {name!r}",
                        _span(import_decl),
                        module=imported_module,
                        name=name,
                    )
                if not symbol.public:
                    fail(
                        "project.resolve.private_import",
                        "resolve",
                        f"declaration {symbol.global_name!r} is private",
                        _span(import_decl),
                        declaration=symbol.global_name,
                    )
                if name in scope.visible:
                    fail(
                        "project.resolve.import_collision",
                        "resolve",
                        f"imported name {name!r} collides in module {module.name!r}",
                        _span(import_decl),
                        module=module.name,
                        name=name,
                    )
                scope.visible[name] = symbol
                if symbol.kind == "type":
                    owner_scope = scopes[symbol.module]
                    prefix = symbol.name + "::"
                    for local_name, constructor in owner_scope.constructors.items():
                        if local_name.startswith(prefix):
                            scope.constructors[local_name] = constructor
    return scopes


def _lower_enum(scope: _Scope, declaration: Any) -> EnumDecl:
    symbol = scope.own[declaration.name]
    constructors = []
    for constructor in _sequence(declaration, "constructors"):
        fields = tuple(
            FieldDecl(field.name, _resolve_type(scope, field.type, set(declaration.type_parameters)), _span(field))
            for field in _sequence(constructor, "fields")
        )
        constructors.append(
            ConstructorDecl(
                f"{symbol.global_name}::{constructor.name}", fields, _span(constructor)
            )
        )
    return EnumDecl(symbol.global_name, tuple(constructors), _span(declaration), tuple(declaration.type_parameters))


def _lower_function(scope: _Scope, declaration: Any, *, version: int = 0) -> Definition:
    symbol = scope.own[declaration.name]
    parameters = _sequence(declaration, "parameters")
    type_parameters = tuple(getattr(declaration, "type_parameters", ()))
    type_variables = set(type_parameters)
    if not parameters and version == 0:
        fail(
            "project.resolve.zero_parameter_function",
            "resolve",
            "Source v0 functions require at least one parameter; use value for data",
            _span(declaration),
            function=symbol.global_name,
        )
    result = _resolve_type(scope, declaration.result_type, type_variables)
    type_: CoreType = result
    for parameter in reversed(parameters):
        type_ = FunctionType(_resolve_type(scope, parameter.type, type_variables), type_)
    for type_parameter in reversed(type_parameters):
        type_ = ForAllType(type_parameter, type_)
    locals_: dict[str, CoreType] = {}
    used: set[str] = set()
    binders: list[Binder] = []
    if not parameters:
        unit = NamedType("core::Unit")
        type_ = FunctionType(unit, type_)
        binders.append(Binder("_unit", unit, _span(declaration)))
        used.add("_unit")
    for parameter in parameters:
        binder = _new_binder(scope, parameter, locals_, used, type_variables=type_variables)
        binders.append(binder)
    expression = _lower_body(scope, declaration.body, locals_, used, type_variables)
    for binder in reversed(binders):
        expression = LamExpr(binder, expression, _span(declaration))
    for type_parameter in reversed(type_parameters):
        expression = LamExpr(TypeBinder(type_parameter, _span(declaration)), expression, _span(declaration))
    return Definition(symbol.global_name, type_, expression, _span(declaration))


def _lower_value(scope: _Scope, declaration: Any) -> Definition:
    symbol = scope.own[declaration.name]
    type_ = _resolve_type(scope, declaration.type)
    expression = _lower_body(scope, declaration.value, {}, set(), set())
    return Definition(symbol.global_name, type_, expression, _span(declaration))


def _lower_body(
    scope: _Scope,
    body: Any,
    locals_: dict[str, CoreType],
    used: set[str],
    type_variables: set[str],
):
    if _class(body) != "Block":
        return _lower_expr(scope, body, locals_, used, type_variables)
    active = dict(locals_)
    lowered: list[tuple[Binder, Any]] = []
    for binding in _sequence(body, "bindings", "lets"):
        value = _lower_expr(scope, binding.value, active, used, type_variables)
        inferred = (
            _infer_lowered_type(scope, value, active)
            if _class(binding.binder.type) == "InferredType"
            else None
        )
        binder = _new_binder(
            scope, binding.binder, active, used, inferred_type=inferred, type_variables=type_variables
        )
        lowered.append((binder, value))
    result = _lower_expr(scope, body.result, active, used, type_variables)
    for binder, value in reversed(lowered):
        result = LetExpr(binder, value, result, _span(body))
    return result


def _lower_expr(
    scope: _Scope,
    expression: Any,
    locals_: dict[str, CoreType],
    used: set[str],
    type_variables: set[str],
):
    kind = _class(expression)
    span = _span(expression)
    if kind == "IntExpr":
        return IntExpr(expression.value, span)
    if kind == "BytesExpr":
        return BytesExpr(expression.value, span)
    if kind in ("LocalExpr", "GlobalExpr"):
        name = expression.name
        if name in locals_:
            return LocalExpr(name, span)
        symbol = _resolve_symbol(scope, name, span)
        if symbol.kind == "function":
            if scope.version >= 1:
                return GlobalExpr(symbol.global_name, span)
            fail("project.resolve.escaping_function", "resolve", "Source v0 functions may only appear as the direct target of a saturated call", span, function=symbol.global_name)
        if symbol.kind != "value":
            fail(
                "project.resolve.expected_value",
                "resolve",
                f"{name!r} names a type, not a value",
                span,
                name=name,
            )
        return GlobalExpr(symbol.global_name, span)
    if kind == "CallExpr":
        target = expression.function
        if scope.version >= 1:
            if _class(target) in ("LocalExpr", "GlobalExpr") and target.name in locals_:
                result = LocalExpr(target.name, _span(target))
            else:
                result = _lower_expr(scope, target, locals_, used, type_variables)
            source_type_arguments = tuple(getattr(expression, "type_arguments", ()))
            target_symbol = None
            if _class(target) in ("LocalExpr", "GlobalExpr") and target.name not in locals_:
                target_symbol = _resolve_symbol(scope, target.name, _span(target))
            if target_symbol is not None:
                expected_types = len(getattr(target_symbol.declaration, "type_parameters", ())) if target_symbol.kind == "function" else 0
                if len(source_type_arguments) != expected_types:
                    fail(
                        "project.resolve.type_argument_arity",
                        "resolve",
                        f"function {target_symbol.global_name!r} expects {expected_types} type arguments, got {len(source_type_arguments)}",
                        span,
                        function=target_symbol.global_name,
                        expected=expected_types,
                        actual=len(source_type_arguments),
                    )
            elif source_type_arguments:
                fail("project.resolve.dynamic_type_application", "resolve", "type arguments require a directly named generic function", span)
            for source_type in source_type_arguments:
                result = AppExpr(result, TypeExpr(_resolve_type(scope, source_type, type_variables), _span(source_type)), span)
            arguments = _sequence(expression, "arguments", "args")
            if not arguments:
                lowered_arguments = (ConExpr("core::Unit::Unit", (), span),)
            else:
                lowered_arguments = tuple(
                    _lower_expr(scope, argument, locals_, used, type_variables) for argument in arguments
                )
            for argument in lowered_arguments:
                result = AppExpr(result, argument, span)
            return result
        if _class(target) not in ("LocalExpr", "GlobalExpr"):
            fail(
                "project.resolve.dynamic_call",
                "resolve",
                "Source v0 calls must directly name a top-level function",
                span,
            )
        if target.name in locals_:
            fail(
                "project.resolve.dynamic_call",
                "resolve",
                "Source v0 cannot call a local value",
                _span(target),
                name=target.name,
            )
        symbol = _resolve_symbol(scope, target.name, _span(target))
        if symbol.kind != "function":
            fail(
                "project.resolve.dynamic_call",
                "resolve",
                "Source v0 calls must directly name a top-level function",
                _span(target),
                name=target.name,
            )
        arguments = _sequence(expression, "arguments", "args")
        expected = len(_sequence(symbol.declaration, "parameters"))
        if len(arguments) != expected:
            fail(
                "project.resolve.call_arity",
                "resolve",
                f"function {symbol.global_name!r} expects {expected} arguments, got {len(arguments)}",
                span,
                function=symbol.global_name,
                expected=expected,
                actual=len(arguments),
            )
        result = GlobalExpr(symbol.global_name, _span(target))
        if expected == 0:
            return AppExpr(result, ConExpr("core::Unit::Unit", (), span), span)
        for argument in arguments:
            result = AppExpr(
                result, _lower_expr(scope, argument, locals_, used, type_variables), span
            )
        return result
    if kind == "LambdaExpr":
        active = dict(locals_)
        binders: list[Binder] = []
        parameters = _sequence(expression, "parameters")
        if not parameters:
            unit = NamedType("core::Unit")
            binders.append(Binder("_unit", unit, span))
        for parameter in parameters:
            binders.append(_new_binder(scope, parameter, active, used, type_variables=type_variables))
        body = _lower_body(scope, expression.body, active, used, type_variables)
        for binder in reversed(binders):
            body = LamExpr(binder, body, span)
        return body
    if kind == "IfExpr":
        condition = _lower_expr(scope, expression.condition, locals_, used, type_variables)
        then_body = _lower_body(scope, expression.then_body, dict(locals_), used, type_variables)
        else_body = _lower_body(scope, expression.else_body, dict(locals_), used, type_variables)
        then_type = _infer_lowered_type(scope, then_body, locals_)
        else_type = _infer_lowered_type(scope, else_body, locals_)
        if then_type != else_type:
            fail(
                "project.type.if_branches",
                "type",
                "if branches must return the same type",
                span,
            )
        return CaseExpr(
            condition,
            then_type,
            (
                Alternative("core::Bool::False", (), else_body, span),
                Alternative("core::Bool::True", (), then_body, span),
            ),
            span,
        )
    if kind == "ConstructorExpr":
        source_constructor = getattr(expression, "constructor", None)
        if isinstance(source_constructor, str):
            constructor_name = source_constructor
        else:
            constructor_name = source_constructor.name
        constructor, constructor_decl, owner_decl = _resolve_constructor(scope, constructor_name, span)
        source_type_arguments = tuple(getattr(expression, "type_arguments", ()))
        expected_types = len(getattr(owner_decl, "type_parameters", ()))
        if len(source_type_arguments) != expected_types:
            fail("project.resolve.constructor_type_arity", "resolve", f"constructor {constructor!r} expects {expected_types} type arguments, got {len(source_type_arguments)}", span, constructor=constructor, expected=expected_types, actual=len(source_type_arguments))
        lowered_types = tuple(_resolve_type(scope, item, type_variables) for item in source_type_arguments)
        return ConExpr(
            constructor,
            tuple(
                _lower_expr(scope, field, locals_, used, type_variables)
                for field in _sequence(expression, "arguments", "args")
            ),
            span,
            lowered_types,
        )
    if kind == "PrimitiveExpr":
        if expression.name.startswith(("foreign.", "runtime.")) and not scope.module.bundled:
            fail(
                "project.resolve.trusted_primitive",
                "resolve",
                "foreign and runtime primitives are restricted to bundled libraries",
                span,
                primitive=expression.name,
            )
        return PrimExpr(
            expression.name,
            tuple(
                _lower_expr(scope, argument, locals_, used, type_variables)
                for argument in _sequence(expression, "arguments", "args")
            ),
            span,
        )
    if kind == "CaseExpr":
        scrutinee = _lower_expr(scope, expression.scrutinee, locals_, used, type_variables)
        result_type = _resolve_type(scope, expression.result_type, type_variables)
        scrutinee_type = _infer_lowered_type(scope, scrutinee, locals_)
        alternatives: list[Alternative] = []
        for alternative in _sequence(expression, "alternatives"):
            constructor, constructor_decl, owner_decl = _resolve_constructor(
                scope, alternative.constructor, _span(alternative)
            )
            active = dict(locals_)
            binders: list[Binder] = []
            source_binders = _sequence(alternative, "binders")
            fields = _sequence(constructor_decl, "fields")
            owner_parameters = tuple(getattr(owner_decl, "type_parameters", ()))
            owner_arguments = scrutinee_type.arguments if isinstance(scrutinee_type, AppliedType) else ()
            substitutions = dict(zip(owner_parameters, owner_arguments, strict=True)) if len(owner_parameters) == len(owner_arguments) else {}
            if len(source_binders) != len(fields):
                fail(
                    "project.resolve.pattern_arity",
                    "resolve",
                    f"constructor pattern expects {len(fields)} binders",
                    _span(alternative),
                    constructor=constructor,
                )
            for source_binder, field in zip(source_binders, fields, strict=True):
                inferred = (
                    _substitute_type(_resolve_type(scope, field.type, set(owner_parameters)), substitutions)
                    if _class(source_binder.type) == "InferredType"
                    else None
                )
                binders.append(
                    _new_binder(
                        scope,
                        source_binder,
                        active,
                        used,
                        inferred_type=inferred,
                        type_variables=type_variables,
                    )
                )
            alternatives.append(
                Alternative(
                    constructor,
                    tuple(binders),
                    _lower_body(scope, alternative.body, active, used, type_variables),
                    _span(alternative),
                )
            )
        return CaseExpr(scrutinee, result_type, tuple(alternatives), span)
    fail(
        "project.lower.expression",
        "lower",
        f"unsupported source expression {kind}",
        span,
        expression=kind,
    )


def _resolve_type(scope: _Scope, source_type: Any, type_variables: set[str] | None = None) -> CoreType:
    type_variables = type_variables or set()
    if _class(source_type) == "IntType":
        return INT
    if _class(source_type) == "NamedType":
        if source_type.name in type_variables:
            return TypeVariable(source_type.name)
        if source_type.name in ("Bool", "core::Bool"):
            return NamedType("core::Bool")
        if source_type.name in ("Unit", "core::Unit"):
            return NamedType("core::Unit")
        if source_type.name in ("Bytes", "core::Bytes"):
            return NamedType("core::Bytes")
        symbol = _resolve_type_symbol(scope, source_type.name, _span(source_type))
        if symbol.kind != "type":
            fail(
                "project.resolve.expected_type",
                "resolve",
                f"{source_type.name!r} does not name a type",
                _span(source_type),
                name=source_type.name,
            )
        expected = len(getattr(symbol.declaration, "type_parameters", ()))
        if expected:
            fail("project.resolve.type_argument_arity", "resolve", f"type {symbol.global_name!r} expects {expected} type arguments, got 0", _span(source_type), type=symbol.global_name, expected=expected, actual=0)
        return NamedType(symbol.global_name)
    if _class(source_type) == "AppliedType":
        symbol = _resolve_type_symbol(scope, source_type.constructor, _span(source_type))
        if symbol.kind != "type":
            fail("project.resolve.expected_type", "resolve", f"{source_type.constructor!r} does not name a type", _span(source_type), name=source_type.constructor)
        arguments = tuple(_resolve_type(scope, item, type_variables) for item in source_type.arguments)
        expected = len(getattr(symbol.declaration, "type_parameters", ()))
        if len(arguments) != expected:
            fail("project.resolve.type_argument_arity", "resolve", f"type {symbol.global_name!r} expects {expected} type arguments, got {len(arguments)}", _span(source_type), type=symbol.global_name, expected=expected, actual=len(arguments))
        return AppliedType(symbol.global_name, arguments)
    if _class(source_type) == "FunctionType":
        return FunctionType(
            _resolve_type(scope, source_type.parameter, type_variables),
            _resolve_type(scope, source_type.result, type_variables),
        )
    fail(
        "project.lower.type",
        "lower",
        f"unsupported source type {_class(source_type)}",
        _span(source_type),
    )


def _resolve_symbol(scope: _Scope, name: str, span: Span) -> _Symbol:
    if "::" not in name:
        symbol = scope.visible.get(name)
        if symbol is not None:
            return symbol
    else:
        for symbol in scope.visible.values():
            if symbol.global_name == name:
                return symbol
    fail(
        "project.resolve.unknown_name",
        "resolve",
        f"unknown or inaccessible name {name!r}",
        span,
        name=name,
        module=scope.module.name,
    )


def _resolve_type_symbol(scope: _Scope, name: str, span: Span) -> _Symbol:
    short = name.removeprefix("core::")
    if "::" not in name and short in scope.visible:
        return scope.visible[short]
    if ("::" not in name or name.startswith("core::")) and "::" not in short and short in scope.core_types:
        return scope.core_types[short]
    return _resolve_symbol(scope, name, span)


def _resolve_constructor(scope: _Scope, name: str, span: Span) -> tuple[str, Any, Any]:
    builtins = {
        "Bool::False": ("core::Bool::False", ()),
        "Bool::True": ("core::Bool::True", ()),
        "core::Bool::False": ("core::Bool::False", ()),
        "core::Bool::True": ("core::Bool::True", ()),
        "Unit::Unit": ("core::Unit::Unit", ()),
        "core::Unit::Unit": ("core::Unit::Unit", ()),
    }
    if name in builtins:
        global_name, fields = builtins[name]
        owner_name = global_name.rsplit("::", 1)[0].rsplit("::", 1)[-1]
        owner = scope.core_types.get(owner_name)
        owner_decl = owner.declaration if owner is not None else type("BuiltinType", (), {"type_parameters": ()})()
        return global_name, type("BuiltinConstructor", (), {"fields": fields})(), owner_decl
    if name in scope.constructors:
        global_name, constructor = scope.constructors[name]
        owner_name = global_name.rsplit("::", 1)[0]
        owner = next((symbol for symbol in (*scope.visible.values(), *scope.core_types.values()) if symbol.global_name == owner_name), None)
        if owner is None:
            raise AssertionError(f"missing owner for constructor {global_name}")
        return global_name, constructor, owner.declaration
    for local_name, result in scope.constructors.items():
        if result[0] == name:
            global_name, constructor = result
            owner_name = global_name.rsplit("::", 1)[0]
            owner = next((symbol for symbol in (*scope.visible.values(), *scope.core_types.values()) if symbol.global_name == owner_name), None)
            if owner is None:
                raise AssertionError(f"missing owner for constructor {global_name}")
            return global_name, constructor, owner.declaration
    fail(
        "project.resolve.unknown_constructor",
        "resolve",
        f"unknown or inaccessible constructor {name!r}",
        span,
        constructor=name,
    )


def _new_binder(
    scope: _Scope,
    source_binder: Any,
    locals_: dict[str, CoreType],
    used: set[str],
    inferred_type: CoreType | None = None,
    type_variables: set[str] | None = None,
) -> Binder:
    name = source_binder.name
    if name in used or name in scope.visible:
        fail(
            "project.resolve.shadowing",
            "resolve",
            f"binder {name!r} shadows or reuses a visible name",
            _span(source_binder),
            name=name,
        )
    type_ = inferred_type or _resolve_type(scope, source_binder.type, type_variables)
    used.add(name)
    locals_[name] = type_
    return Binder(name, type_, _span(source_binder))


def _infer_lowered_type(
    scope: _Scope, expression: Any, locals_: dict[str, CoreType]
) -> CoreType:
    if isinstance(expression, IntExpr): return INT
    if isinstance(expression, BytesExpr): return NamedType("core::Bytes")
    if isinstance(expression, LocalExpr): return locals_[expression.name]
    if isinstance(expression, GlobalExpr):
        for symbol in scope.visible.values():
            if symbol.global_name == expression.name:
                if symbol.kind == "function":
                    defining_scope = scope.scopes[symbol.module]
                    parameters = tuple(getattr(symbol.declaration, "type_parameters", ()))
                    variables = set(parameters)
                    result = _resolve_type(defining_scope, symbol.declaration.result_type, variables)
                    for parameter in reversed(_sequence(symbol.declaration, "parameters")):
                        result = FunctionType(_resolve_type(defining_scope, parameter.type, variables), result)
                    if not _sequence(symbol.declaration, "parameters"):
                        result = FunctionType(NamedType("core::Unit"), result)
                    for parameter in reversed(parameters):
                        result = ForAllType(parameter, result)
                    return result
                return _resolve_type(scope.scopes[symbol.module], symbol.declaration.type)
        raise AssertionError(f"unresolved global {expression.name}")
    if isinstance(expression, LamExpr):
        if isinstance(expression.binder, TypeBinder):
            return ForAllType(expression.binder.name, _infer_lowered_type(scope, expression.body, locals_))
        return FunctionType(expression.binder.type, _infer_lowered_type(scope, expression.body, locals_ | {expression.binder.name: expression.binder.type}))
    if isinstance(expression, AppExpr):
        function = _infer_lowered_type(scope, expression.function, locals_)
        if isinstance(expression.argument, TypeExpr):
            if not isinstance(function, ForAllType):
                fail("project.infer.not_polymorphic", "type", "type application target is not polymorphic", expression.span)
            return _substitute_type(function.body, {function.parameter: expression.argument.type})
        if not isinstance(function, FunctionType):
            fail("project.infer.not_function", "type", "application target is not a function", expression.span)
        return function.result
    if isinstance(expression, LetExpr):
        return _infer_lowered_type(scope, expression.body, locals_ | {expression.binder.name: expression.binder.type})
    if isinstance(expression, PrimExpr):
        if expression.name in ("int.equal", "int.less"): return NamedType("core::Bool")
        if expression.name == "int.to_bytes": return NamedType("core::Bytes")
        if expression.name == "bytes.length": return INT
        if expression.name == "runtime.trap": return NamedType("core::Unit")
        binding = scope.foreign_bindings.get(expression.name)
        if binding is not None: return binding.result
        return INT
    if isinstance(expression, ConExpr):
        owner = expression.constructor.rsplit("::", 1)[0]
        return AppliedType(owner, expression.type_arguments) if expression.type_arguments else NamedType(owner)
    if isinstance(expression, CaseExpr): return expression.result_type
    raise AssertionError(f"cannot infer {type(expression).__name__}")


def _validate_entry(project: Project, unit: CoreUnit) -> None:
    definitions = {definition.name: definition for definition in unit.definitions}
    entry = definitions.get(project.manifest.entry)
    if entry is None:
        fail(
            "project.entry.missing",
            "resolve",
            f"entry {project.manifest.entry!r} does not name a project value",
            entry=project.manifest.entry,
        )
    if isinstance(entry.type, FunctionType):
        expected = FunctionType(
            NamedType("core::Unit"), NamedType("os::process::Exit")
        )
        if unit.version != 2 or entry.type != expected:
            fail(
                "project.entry.function",
                "resolve",
                "v1 function entry must have type fn() -> Exit",
                entry=project.manifest.entry,
            )


def _sequence(value: Any, *names: str) -> tuple[Any, ...]:
    for name in names:
        if hasattr(value, name):
            return tuple(getattr(value, name))
    return ()


def _import_module(import_decl: Any) -> str:
    return getattr(import_decl, "module", getattr(import_decl, "name", None))


def _span(value: Any) -> Span:
    span = getattr(value, "span", UNKNOWN_SPAN)
    return span if isinstance(span, Span) else UNKNOWN_SPAN


def _class(value: Any) -> str:
    return type(value).__name__


def _substitute_type(type_: CoreType, substitutions: dict[str, CoreType]) -> CoreType:
    if isinstance(type_, TypeVariable):
        return substitutions.get(type_.name, type_)
    if isinstance(type_, AppliedType):
        return AppliedType(type_.constructor, tuple(_substitute_type(item, substitutions) for item in type_.arguments))
    if isinstance(type_, FunctionType):
        return FunctionType(_substitute_type(type_.parameter, substitutions), _substitute_type(type_.result, substitutions))
    if isinstance(type_, ForAllType):
        nested = dict(substitutions)
        nested.pop(type_.parameter, None)
        return ForAllType(type_.parameter, _substitute_type(type_.body, nested))
    return type_

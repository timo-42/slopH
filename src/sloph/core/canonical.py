from __future__ import annotations

from dataclasses import replace
from typing import Iterable, TypeAlias
from urllib.parse import quote

from sloph.core.model import (
    Alternative,
    AppExpr,
    Binder,
    BytesExpr,
    CaseExpr,
    ConExpr,
    CoreType,
    CoreUnit,
    Definition,
    EnumDecl,
    Expr,
    FunctionType,
    ForeignBinding,
    GlobalExpr,
    IntExpr,
    IntType,
    LamExpr,
    LetExpr,
    LocalExpr,
    NamedType,
    PrimExpr,
)
from sloph.core.diagnostics import limit_fail
from sloph.core.limits import Limits
from sloph.core.validate import validate


Form: TypeAlias = str | tuple["Form", ...]


def canonicalize(unit: CoreUnit) -> CoreUnit:
    validate(unit)
    constructors = {
        constructor.name: enum
        for enum in unit.types
        for constructor in enum.constructors
    }
    definitions = tuple(
        _alpha_definition(definition, constructors)
        for definition in sorted(unit.definitions, key=lambda item: item.name)
    )
    return replace(
        unit,
        types=tuple(sorted(unit.types, key=lambda item: item.name)),
        definitions=definitions,
        foreign_bindings=tuple(sorted(unit.foreign_bindings, key=lambda item: item.identity)),
    )


def format_core(unit: CoreUnit, limits: Limits | None = None) -> str:
    limits = limits or Limits()
    canonical = canonicalize(unit)
    if _unit_flat_size(canonical) > limits.output_bytes:
        limit_fail("print", "output_bytes", limits.output_bytes, unit.span)
    items: tuple[Form, ...] = (
        "core",
        str(canonical.version),
        ("types", *(_enum_form(enum) for enum in canonical.types)),
        ("defs", *(_definition_form(definition) for definition in canonical.definitions)),
    )
    if canonical.foreign_bindings:
        items += (("foreign", *(_foreign_form(item) for item in canonical.foreign_bindings)),)
    form: Form = items
    result = _pretty(form, 0) + "\n"
    if len(result.encode("ascii")) > limits.output_bytes:
        limit_fail("print", "output_bytes", limits.output_bytes, unit.span)
    return result


def _list_size(items: Iterable[int]) -> int:
    count = 0
    size = 2
    for item in items:
        if count:
            size += 1
        size += item
        count += 1
    return size


def _unit_flat_size(unit: CoreUnit) -> int:
    types = _list_size((len("types"), *(_enum_size(item) for item in unit.types)))
    definitions = _list_size(
        (len("defs"), *(_definition_size(item) for item in unit.definitions))
    )
    foreign = _list_size((len("foreign"), *(_foreign_size(item) for item in unit.foreign_bindings))) if unit.foreign_bindings else 0
    parts = (len("core"), 1, types, definitions) + ((foreign,) if foreign else ())
    return _list_size(parts)


def _encoded(value: str) -> str:
    return quote(value, safe="._:-")


def _foreign_form(binding: ForeignBinding) -> Form:
    return (
        "binding", binding.identity, binding.symbol, binding.adapter,
        ("params", *(_type_form(item) for item in binding.parameters)),
        ("result", _type_form(binding.result)),
        ("c-params", *(_encoded(item) for item in binding.c_parameters)),
        ("c-result", _encoded(binding.c_result)),
        ("requires", *binding.requires), ("effects", *binding.effects),
        ("targets", *binding.targets),
        ("facts", *(("fact", key, value) for key, value in binding.facts)),
        ("provenance", binding.provenance),
    )


def _foreign_size(binding: ForeignBinding) -> int:
    return len(str(_foreign_form(binding))) * 2


def _enum_size(enum: EnumDecl) -> int:
    return _list_size(
        (len("enum"), len(enum.name), *(_constructor_size(item) for item in enum.constructors))
    )


def _constructor_size(constructor) -> int:
    return _list_size(
        (
            len("ctor"),
            len(constructor.name),
            *(
                _list_size((len("field"), len(field.name), _type_size(field.type)))
                for field in constructor.fields
            ),
        )
    )


def _definition_size(definition: Definition) -> int:
    return _list_size(
        (
            len("def"),
            len(definition.name),
            _type_size(definition.type),
            _expression_size(definition.value),
        )
    )


def _type_size(type_: CoreType) -> int:
    if isinstance(type_, IntType):
        return len("Int")
    if isinstance(type_, NamedType):
        return _list_size((len("named"), len(type_.name)))
    if isinstance(type_, FunctionType):
        return _list_size((len("fn"), _type_size(type_.parameter), _type_size(type_.result)))
    raise AssertionError(f"unsupported type {type(type_)!r}")


def _binder_size(binder: Binder) -> int:
    return _list_size((len("bind"), len(binder.name), _type_size(binder.type)))


def _expression_size(expression: Expr) -> int:
    if isinstance(expression, IntExpr):
        return _list_size((len("int"), len(decimal_string(expression.value))))
    if isinstance(expression, BytesExpr):
        return _list_size((len("bytes"), 1 + len(expression.value) * 2))
    if isinstance(expression, LocalExpr):
        return _list_size((len("local"), len(expression.name)))
    if isinstance(expression, GlobalExpr):
        return _list_size((len("global"), len(expression.name)))
    if isinstance(expression, LamExpr):
        return _list_size((len("lam"), _binder_size(expression.binder), _expression_size(expression.body)))
    if isinstance(expression, AppExpr):
        return _list_size((len("app"), _expression_size(expression.function), _expression_size(expression.argument)))
    if isinstance(expression, LetExpr):
        return _list_size(
            (
                len("let"),
                _binder_size(expression.binder),
                _expression_size(expression.value),
                _expression_size(expression.body),
            )
        )
    if isinstance(expression, PrimExpr):
        return _list_size(
            (len("prim"), len(expression.name), *(_expression_size(item) for item in expression.arguments))
        )
    if isinstance(expression, ConExpr):
        return _list_size(
            (len("con"), len(expression.constructor), *(_expression_size(item) for item in expression.fields))
        )
    if isinstance(expression, CaseExpr):
        return _list_size(
            (
                len("case"),
                _expression_size(expression.scrutinee),
                _type_size(expression.result_type),
                *(_alternative_size(item) for item in expression.alternatives),
            )
        )
    raise AssertionError(f"unsupported expression {type(expression)!r}")


def _alternative_size(alternative: Alternative) -> int:
    return _list_size(
        (
            len("alt"),
            len(alternative.constructor),
            *(_binder_size(item) for item in alternative.binders),
            _expression_size(alternative.body),
        )
    )


def decimal_string(value: int) -> str:
    if value == 0:
        return "0"
    negative = value < 0
    remaining = -value if negative else value
    chunks: list[int] = []
    base = 1_000_000_000
    while remaining:
        remaining, chunk = divmod(remaining, base)
        chunks.append(chunk)
    result = str(chunks.pop())
    while chunks:
        result += f"{chunks.pop():09d}"
    return "-" + result if negative else result


def _alpha_definition(
    definition: Definition, constructors: dict[str, EnumDecl]
) -> Definition:
    counter = [0]
    value = _rename_expr(definition.value, {}, counter, constructors)
    return replace(definition, value=value)


def _fresh(counter: list[int]) -> str:
    name = f"v{counter[0]}"
    counter[0] += 1
    return name


def _rename_expr(
    expression: Expr,
    environment: dict[str, str],
    counter: list[int],
    constructors: dict[str, EnumDecl],
) -> Expr:
    if isinstance(expression, (IntExpr, BytesExpr, GlobalExpr)):
        return expression
    if isinstance(expression, LocalExpr):
        return replace(expression, name=environment[expression.name])
    if isinstance(expression, LamExpr):
        new_name = _fresh(counter)
        local_environment = dict(environment)
        local_environment[expression.binder.name] = new_name
        return replace(
            expression,
            binder=replace(expression.binder, name=new_name),
            body=_rename_expr(expression.body, local_environment, counter, constructors),
        )
    if isinstance(expression, AppExpr):
        return replace(
            expression,
            function=_rename_expr(expression.function, environment, counter, constructors),
            argument=_rename_expr(expression.argument, environment, counter, constructors),
        )
    if isinstance(expression, LetExpr):
        new_name = _fresh(counter)
        value = _rename_expr(expression.value, environment, counter, constructors)
        local_environment = dict(environment)
        local_environment[expression.binder.name] = new_name
        return replace(
            expression,
            binder=replace(expression.binder, name=new_name),
            value=value,
            body=_rename_expr(expression.body, local_environment, counter, constructors),
        )
    if isinstance(expression, PrimExpr):
        return replace(
            expression,
            arguments=tuple(
                _rename_expr(item, environment, counter, constructors)
                for item in expression.arguments
            ),
        )
    if isinstance(expression, ConExpr):
        return replace(
            expression,
            fields=tuple(
                _rename_expr(item, environment, counter, constructors)
                for item in expression.fields
            ),
        )
    if isinstance(expression, CaseExpr):
        scrutinee = _rename_expr(expression.scrutinee, environment, counter, constructors)
        alternatives = {item.constructor: item for item in expression.alternatives}
        if not alternatives:
            return replace(
                expression, scrutinee=scrutinee, alternatives=()
            )
        # Validation guarantees a named scrutinee and exhaustive alternatives.
        enum = constructors[next(iter(alternatives))]
        renamed: list[Alternative] = []
        for constructor in enum.constructors:
            alternative = alternatives[constructor.name]
            local_environment = dict(environment)
            binders: list[Binder] = []
            for binder in alternative.binders:
                new_name = _fresh(counter)
                local_environment[binder.name] = new_name
                binders.append(replace(binder, name=new_name))
            renamed.append(
                replace(
                    alternative,
                    binders=tuple(binders),
                    body=_rename_expr(
                        alternative.body, local_environment, counter, constructors
                    ),
                )
            )
        return replace(
            expression, scrutinee=scrutinee, alternatives=tuple(renamed)
        )
    raise AssertionError(f"unsupported expression {type(expression)!r}")


def _enum_form(enum: EnumDecl) -> Form:
    return (
        "enum",
        enum.name,
        *(
            (
                "ctor",
                constructor.name,
                *(
                    ("field", field.name, _type_form(field.type))
                    for field in constructor.fields
                ),
            )
            for constructor in enum.constructors
        ),
    )


def _definition_form(definition: Definition) -> Form:
    return ("def", definition.name, _type_form(definition.type), _expr_form(definition.value))


def _type_form(type_: CoreType) -> Form:
    if isinstance(type_, IntType):
        return "Int"
    if isinstance(type_, NamedType):
        return ("named", type_.name)
    if isinstance(type_, FunctionType):
        return ("fn", _type_form(type_.parameter), _type_form(type_.result))
    raise AssertionError(f"unsupported type {type(type_)!r}")


def _binder_form(binder: Binder) -> Form:
    return ("bind", binder.name, _type_form(binder.type))


def _expr_form(expression: Expr) -> Form:
    if isinstance(expression, IntExpr):
        return ("int", decimal_string(expression.value))
    if isinstance(expression, BytesExpr):
        return ("bytes", "x" + expression.value.hex())
    if isinstance(expression, LocalExpr):
        return ("local", expression.name)
    if isinstance(expression, GlobalExpr):
        return ("global", expression.name)
    if isinstance(expression, LamExpr):
        return ("lam", _binder_form(expression.binder), _expr_form(expression.body))
    if isinstance(expression, AppExpr):
        return ("app", _expr_form(expression.function), _expr_form(expression.argument))
    if isinstance(expression, LetExpr):
        return (
            "let",
            _binder_form(expression.binder),
            _expr_form(expression.value),
            _expr_form(expression.body),
        )
    if isinstance(expression, PrimExpr):
        return (
            "prim",
            expression.name,
            *(_expr_form(item) for item in expression.arguments),
        )
    if isinstance(expression, ConExpr):
        return (
            "con",
            expression.constructor,
            *(_expr_form(item) for item in expression.fields),
        )
    if isinstance(expression, CaseExpr):
        return (
            "case",
            _expr_form(expression.scrutinee),
            _type_form(expression.result_type),
            *(
                (
                    "alt",
                    alternative.constructor,
                    *(_binder_form(item) for item in alternative.binders),
                    _expr_form(alternative.body),
                )
                for alternative in expression.alternatives
            ),
        )
    raise AssertionError(f"unsupported expression {type(expression)!r}")


def _flat(form: Form) -> str:
    if isinstance(form, str):
        return form
    return "(" + " ".join(_flat(item) for item in form) + ")"


def _pretty(form: Form, indent: int) -> str:
    flat = _flat(form)
    if isinstance(form, str) or indent + len(flat) <= 88:
        return " " * indent + flat
    prefix: list[str] = []
    rest_index = 0
    for rest_index, item in enumerate(form):
        if isinstance(item, str):
            prefix.append(item)
            continue
        break
    else:
        return " " * indent + flat
    result = " " * indent + "(" + " ".join(prefix)
    for item in form[rest_index:]:
        result += "\n" + _pretty(item, indent + 2)
    return result + ")"

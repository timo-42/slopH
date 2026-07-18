from __future__ import annotations

from dataclasses import dataclass
import re
from urllib.parse import unquote

from sloph.core.diagnostics import Span, fail, limit_fail
from sloph.core.limits import Limits
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
    Expr,
    FieldDecl,
    FunctionType,
    ForAllType,
    ForeignBinding,
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


@dataclass(frozen=True, slots=True)
class Atom:
    value: str
    span: Span


@dataclass(frozen=True, slots=True)
class ListNode:
    items: tuple["SExpr", ...]
    span: Span


SExpr = Atom | ListNode

INTEGER_RE = re.compile(r"-?(?:0|[1-9][0-9]*)\Z")


def parse_core(data: bytes | str, limits: Limits | None = None) -> CoreUnit:
    limits = limits or Limits()
    if isinstance(data, str):
        if len(data) > limits.input_bytes:
            limit_fail("parse", "input_bytes", limits.input_bytes, Span(0, len(data)))
        try:
            raw = data.encode("ascii")
        except UnicodeEncodeError as error:
            fail(
                "core.parse.non_ascii",
                "parse",
                "Core v0 input must be ASCII",
                Span(error.start, error.end),
            )
    else:
        raw = data
    root = _parse_sexpr(raw, limits)
    return _decode_unit(root, limits)


def _parse_sexpr(raw: bytes, limits: Limits) -> SExpr:
    if len(raw) > limits.input_bytes:
        limit_fail("parse", "input_bytes", limits.input_bytes, Span(0, len(raw)))
    nul = raw.find(b"\x00")
    if nul >= 0:
        fail("core.parse.nul", "parse", "NUL is not permitted", Span(nul, nul + 1))
    try:
        raw.decode("ascii")
    except UnicodeDecodeError as error:
        fail(
            "core.parse.non_ascii",
            "parse",
            "Core v0 input must be ASCII",
            Span(error.start, error.end),
        )
    for index, byte in enumerate(raw):
        if byte == 13 and (index + 1 >= len(raw) or raw[index + 1] != 10):
            fail(
                "core.parse.bare_cr",
                "parse",
                "bare carriage return is not permitted",
                Span(index, index + 1),
            )

    roots: list[SExpr] = []
    stack: list[tuple[int, list[SExpr]]] = []
    token_count = 0
    node_count = 0
    index = 0

    def token(span: Span) -> None:
        nonlocal token_count
        token_count += 1
        if token_count > limits.tokens:
            limit_fail("parse", "tokens", limits.tokens, span)

    def append(node: SExpr) -> None:
        nonlocal node_count
        node_count += 1
        if node_count > limits.ast_nodes:
            limit_fail("parse", "ast_nodes", limits.ast_nodes, node.span)
        if stack:
            stack[-1][1].append(node)
        else:
            roots.append(node)

    while index < len(raw):
        byte = raw[index]
        if byte in b" \t\n\r":
            index += 1
            continue
        if byte == ord(";"):
            while index < len(raw) and raw[index] not in (10, 13):
                index += 1
            continue
        if byte == ord("("):
            token(Span(index, index + 1))
            if len(stack) + 1 > limits.syntax_depth:
                limit_fail(
                    "parse", "syntax_depth", limits.syntax_depth, Span(index, index + 1)
                )
            stack.append((index, []))
            index += 1
            continue
        if byte == ord(")"):
            token(Span(index, index + 1))
            if not stack:
                fail(
                    "core.parse.unexpected_close",
                    "parse",
                    "unexpected closing parenthesis",
                    Span(index, index + 1),
                )
            start, items = stack.pop()
            append(ListNode(tuple(items), Span(start, index + 1)))
            index += 1
            continue

        start = index
        while index < len(raw) and raw[index] not in b" \t\n\r();":
            index += 1
        if index == start:
            fail(
                "core.parse.invalid_byte",
                "parse",
                "invalid input byte",
                Span(index, index + 1),
            )
        if index - start > limits.token_bytes:
            limit_fail(
                "parse", "token_bytes", limits.token_bytes, Span(start, index)
            )
        token(Span(start, index))
        append(Atom(raw[start:index].decode("ascii"), Span(start, index)))

    if stack:
        start, _ = stack[-1]
        fail(
            "core.parse.unclosed_list",
            "parse",
            "unclosed list",
            Span(start, len(raw)),
        )
    if len(roots) != 1:
        span = Span(0, len(raw))
        fail(
            "core.parse.root_count",
            "parse",
            "Core input must contain exactly one root form",
            span,
            roots=len(roots),
        )
    return roots[0]


def _decode_unit(node: SExpr, limits: Limits) -> CoreUnit:
    items = _tagged(node, "core", minimum=4)
    if len(items) not in (4, 5):
        fail("core.parse.arity", "parse", "core form must contain types, defs, and optional foreign bindings", node.span)
    version_atom = _atom(items[1], "Core version")
    if version_atom.value not in ("0", "1", "2"):
        fail(
            "core.parse.unsupported_version",
            "parse",
            "only Core versions 0, 1, and 2 are supported",
            version_atom.span,
            version=version_atom.value,
        )
    type_items = _tagged(items[2], "types", minimum=1)
    def_items = _tagged(items[3], "defs", minimum=1)
    version = int(version_atom.value)
    types = tuple(_decode_enum(item, limits, version) for item in type_items[1:])
    definitions = tuple(_decode_definition(item, limits, version) for item in def_items[1:])
    bindings: tuple[ForeignBinding, ...] = ()
    if len(items) == 5:
        binding_items = _tagged(items[4], "foreign", minimum=1)
        bindings = tuple(_decode_foreign(item) for item in binding_items[1:])
    return CoreUnit(int(version_atom.value), types, definitions, node.span, bindings)


def _decode_foreign(node: SExpr) -> ForeignBinding:
    items = _tagged(node, "binding", exact=14)
    params = _tagged(items[4], "params", minimum=1)
    result = _tagged(items[5], "result", exact=2)
    c_params = _tagged(items[6], "c-params", minimum=1)
    c_result = _tagged(items[7], "c-result", exact=2)
    provider = _tagged(items[8], "provider", exact=2)
    header = _tagged(items[9], "header", exact=2)
    requires = _tagged(items[10], "requires", minimum=1)
    effects = _tagged(items[11], "effects", minimum=1)
    facts = _tagged(items[12], "facts", minimum=1)
    provenance = _tagged(items[13], "provenance", exact=2)
    pairs = []
    for fact in facts[1:]:
        fact_items = _tagged(fact, "fact", exact=3)
        pairs.append((_atom(fact_items[1], "fact key").value, _atom(fact_items[2], "fact value").value))
    return ForeignBinding(
        _atom(items[1], "binding identity").value,
        _atom(items[2], "C symbol").value,
        tuple(_decode_type(item) for item in params[1:]),
        _decode_type(result[1]),
        _atom(items[3], "adapter").value,
        tuple(unquote(_atom(item, "C parameter").value) for item in c_params[1:]),
        unquote(_atom(c_result[1], "C result").value),
        _atom(provider[1], "provider").value,
        unquote(_atom(header[1], "header").value),
        tuple(_atom(item, "requirement").value for item in requires[1:]),
        tuple(_atom(item, "effect").value for item in effects[1:]),
        tuple(pairs),
        _atom(provenance[1], "provenance").value,
    )


def _decode_enum(node: SExpr, limits: Limits, version: int) -> EnumDecl:
    items = _tagged(node, "enum", minimum=3 if version == 2 else 2)
    name = _atom(items[1], "enum identity")
    offset = 2
    type_parameters: tuple[str, ...] = ()
    if version == 2:
        params = _tagged(items[2], "params", minimum=1)
        type_parameters = tuple(_atom(item, "type parameter").value for item in params[1:])
        offset = 3
    constructors = tuple(_decode_constructor(item, limits) for item in items[offset:])
    return EnumDecl(name.value, constructors, node.span, type_parameters)


def _decode_constructor(node: SExpr, limits: Limits) -> ConstructorDecl:
    items = _tagged(node, "ctor", minimum=2)
    name = _atom(items[1], "constructor identity")
    fields = tuple(_decode_field(item, limits) for item in items[2:])
    return ConstructorDecl(name.value, fields, node.span)


def _decode_field(node: SExpr, limits: Limits) -> FieldDecl:
    items = _tagged(node, "field", exact=3)
    name = _atom(items[1], "field name")
    field_type = _decode_type(items[2])
    if isinstance(field_type, FunctionType):
        fail(
            "core.parse.function_field",
            "parse",
            "Core v0 nominal fields cannot have function type",
            items[2].span,
        )
    return FieldDecl(name.value, field_type, node.span)


def _decode_definition(node: SExpr, limits: Limits, version: int) -> Definition:
    items = _tagged(node, "def", exact=4)
    name = _atom(items[1], "definition identity")
    return Definition(
        name.value,
        _decode_type(items[2]),
        _decode_expr(items[3], limits, version),
        node.span,
    )


def _decode_type(node: SExpr) -> CoreType:
    if isinstance(node, Atom):
        if node.value == "Int":
            return INT
        fail(
            "core.parse.unknown_type",
            "parse",
            f"unknown type form {node.value!r}",
            node.span,
        )
    if not node.items:
        fail("core.parse.empty_type", "parse", "empty type form", node.span)
    tag = _atom(node.items[0], "type tag")
    if tag.value == "named":
        items = _tagged(node, "named", exact=2)
        return NamedType(_atom(items[1], "named type identity").value)
    if tag.value == "var":
        items = _tagged(node, "var", exact=2)
        return TypeVariable(_atom(items[1], "type variable").value)
    if tag.value == "apply":
        items = _tagged(node, "apply", minimum=3)
        return AppliedType(
            _atom(items[1], "type constructor identity").value,
            tuple(_decode_type(item) for item in items[2:]),
        )
    if tag.value == "fn":
        items = _tagged(node, "fn", exact=3)
        return FunctionType(_decode_type(items[1]), _decode_type(items[2]))
    if tag.value == "forall":
        items = _tagged(node, "forall", exact=3)
        return ForAllType(
            _atom(items[1], "type parameter").value,
            _decode_type(items[2]),
        )
    fail(
        "core.parse.unknown_type",
        "parse",
        f"unknown type tag {tag.value!r}",
        tag.span,
    )


def _decode_binder(node: SExpr) -> Binder | TypeBinder:
    if isinstance(node, ListNode) and node.items and isinstance(node.items[0], Atom) and node.items[0].value == "type-bind":
        items = _tagged(node, "type-bind", exact=2)
        return TypeBinder(_atom(items[1], "type binder").value, node.span)
    items = _tagged(node, "bind", exact=3)
    name = _atom(items[1], "binder name")
    return Binder(name.value, _decode_type(items[2]), node.span)


def _decode_expr(node: SExpr, limits: Limits, version: int) -> Expr:
    if not isinstance(node, ListNode) or not node.items:
        fail(
            "core.parse.expression_form",
            "parse",
            "expression must be a non-empty tagged list",
            node.span,
        )
    tag = _atom(node.items[0], "expression tag")
    if tag.value == "int":
        items = _tagged(node, "int", exact=2)
        literal = _atom(items[1], "integer literal")
        return IntExpr(_parse_integer(literal, limits), node.span)
    if tag.value == "bytes":
        items = _tagged(node, "bytes", exact=2)
        literal = _atom(items[1], "byte literal")
        payload = literal.value[1:] if literal.value.startswith("x") else ""
        if not literal.value.startswith("x") or len(payload) % 2 or any(
            character not in "0123456789abcdef" for character in payload
        ):
            fail("core.parse.byte_syntax", "parse", "bytes use 'x' followed by canonical lowercase hexadecimal", literal.span)
        if len(payload) // 2 > limits.input_bytes:
            limit_fail("parse", "input_bytes", limits.input_bytes, literal.span)
        return BytesExpr(bytes.fromhex(payload), node.span)
    if tag.value == "local":
        items = _tagged(node, "local", exact=2)
        return LocalExpr(_atom(items[1], "local name").value, node.span)
    if tag.value == "global":
        items = _tagged(node, "global", exact=2)
        return GlobalExpr(_atom(items[1], "global identity").value, node.span)
    if tag.value == "type":
        items = _tagged(node, "type", exact=2)
        return TypeExpr(_decode_type(items[1]), node.span)
    if tag.value == "lam":
        items = _tagged(node, "lam", exact=3)
        return LamExpr(_decode_binder(items[1]), _decode_expr(items[2], limits, version), node.span)
    if tag.value == "app":
        items = _tagged(node, "app", exact=3)
        return AppExpr(
            _decode_expr(items[1], limits, version),
            _decode_expr(items[2], limits, version),
            node.span,
        )
    if tag.value == "let":
        items = _tagged(node, "let", exact=4)
        return LetExpr(
            _decode_binder(items[1]),
            _decode_expr(items[2], limits, version),
            _decode_expr(items[3], limits, version),
            node.span,
        )
    if tag.value == "prim":
        items = _tagged(node, "prim", minimum=2)
        name = _atom(items[1], "primitive name")
        return PrimExpr(
            name.value,
            tuple(_decode_expr(item, limits, version) for item in items[2:]),
            node.span,
        )
    if tag.value == "con":
        items = _tagged(node, "con", minimum=3 if version == 2 else 2)
        name = _atom(items[1], "constructor identity")
        offset = 2
        type_arguments: tuple[CoreType, ...] = ()
        if version == 2:
            types = _tagged(items[2], "types", minimum=1)
            type_arguments = tuple(_decode_type(item) for item in types[1:])
            offset = 3
        return ConExpr(
            name.value,
            tuple(_decode_expr(item, limits, version) for item in items[offset:]),
            node.span,
            type_arguments,
        )
    if tag.value == "case":
        items = _tagged(node, "case", minimum=3)
        return CaseExpr(
            _decode_expr(items[1], limits, version),
            _decode_type(items[2]),
            tuple(_decode_alternative(item, limits, version) for item in items[3:]),
            node.span,
        )
    fail(
        "core.parse.unknown_expression",
        "parse",
        f"unknown expression tag {tag.value!r}",
        tag.span,
    )


def _decode_alternative(node: SExpr, limits: Limits, version: int) -> Alternative:
    items = _tagged(node, "alt", minimum=3)
    constructor = _atom(items[1], "alternative constructor")
    binders = tuple(_decode_binder(item) for item in items[2:-1])
    body = _decode_expr(items[-1], limits, version)
    return Alternative(constructor.value, binders, body, node.span)


def _parse_integer(atom: Atom, limits: Limits) -> int:
    text = atom.value
    if not INTEGER_RE.fullmatch(text):
        fail(
            "core.parse.integer_syntax",
            "parse",
            "integer literals use canonical decimal syntax",
            atom.span,
        )
    digits = text[1:] if text.startswith("-") else text
    if len(digits) > limits.literal_digits:
        limit_fail("parse", "literal_digits", limits.literal_digits, atom.span)
    value = 0
    for digit in digits:
        value = value * 10 + (ord(digit) - ord("0"))
    return -value if text.startswith("-") else value


def _tagged(
    node: SExpr,
    expected: str,
    *,
    exact: int | None = None,
    minimum: int | None = None,
) -> tuple[SExpr, ...]:
    if not isinstance(node, ListNode) or not node.items:
        fail(
            "core.parse.expected_form",
            "parse",
            f"expected ({expected} ...)",
            node.span,
        )
    tag = _atom(node.items[0], f"{expected} tag")
    if tag.value != expected:
        fail(
            "core.parse.unexpected_tag",
            "parse",
            f"expected tag {expected!r}, found {tag.value!r}",
            tag.span,
            expected=expected,
            actual=tag.value,
        )
    if exact is not None and len(node.items) != exact:
        fail(
            "core.parse.wrong_arity",
            "parse",
            f"{expected} expects {exact - 1} arguments",
            node.span,
            expected=exact - 1,
            actual=len(node.items) - 1,
        )
    if minimum is not None and len(node.items) < minimum:
        fail(
            "core.parse.wrong_arity",
            "parse",
            f"{expected} expects at least {minimum - 1} arguments",
            node.span,
            expected_minimum=minimum - 1,
            actual=len(node.items) - 1,
        )
    return node.items


def _atom(node: SExpr, description: str) -> Atom:
    if not isinstance(node, Atom):
        fail(
            "core.parse.expected_atom",
            "parse",
            f"{description} must be an atom",
            node.span,
        )
    return node

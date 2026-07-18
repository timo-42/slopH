from __future__ import annotations

from dataclasses import dataclass

from sloph.core.diagnostics import DiagnosticError, Span, fail
from sloph.core.limits import Limits
from sloph.syntax._integer import parse_decimal
from sloph.syntax.model import (
    Binder, Block, CallExpr, CaseAlternative, CaseExpr, ConstructorDecl,
    ConstructorExpr, FieldDecl, FunctionDecl, GlobalExpr, ImportDecl, IntExpr,
    IntType, LetBinding, LocalExpr, Module, NamedType, PrimitiveExpr, TypeDecl,
    TypeRef, ValueDecl,
)


@dataclass(frozen=True, slots=True)
class _Token:
    text: str
    start: int
    end: int


_PUNCT = frozenset("{}(),;:=|")
_KEYWORDS = frozenset({"module", "import", "public", "type", "fn", "value", "let", "case", "primitive"})


def _limit(name: str, configured: int, span: Span = Span(0, 0)) -> None:
    fail("syntax.parse.limit_exceeded", "parse", f"{name} limit exceeded (configured {configured})", span,
         limit=name, configured=configured)


def _source_bytes(source: str | bytes, limits: Limits) -> bytes:
    if isinstance(source, str):
        if len(source) > limits.input_bytes:
            _limit("input_bytes", limits.input_bytes)
        try:
            data = source.encode("ascii")
        except UnicodeEncodeError as error:
            fail("syntax.parse.non_ascii", "parse", "source must contain ASCII only", Span(error.start, error.end))
    elif isinstance(source, bytes):
        data = source
    else:
        raise TypeError("source must be str or bytes")
    if len(data) > limits.input_bytes:
        _limit("input_bytes", limits.input_bytes)
    try:
        data.decode("ascii")
    except UnicodeDecodeError as error:
        fail("syntax.parse.non_ascii", "parse", "source must contain ASCII only", Span(error.start, error.end))
    return data


def _lex(data: bytes, limits: Limits, *, version: int = 0) -> tuple[_Token, ...]:
    text = data.decode("ascii")
    out: list[_Token] = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "\r":
            if i + 1 >= len(text) or text[i + 1] != "\n":
                fail("syntax.parse.bare_carriage_return", "parse", "bare carriage return is not allowed", Span(i, i + 1))
            i += 2
            continue
        if ch in " \t\n":
            i += 1
            continue
        if version == 1 and text.startswith("//", i):
            newline = text.find("\n", i + 2)
            i = len(text) if newline < 0 else newline
            continue
        if version == 1 and text.startswith("/*", i):
            start = i
            depth = 1
            i += 2
            while depth and i < len(text):
                if text.startswith("/*", i):
                    depth += 1
                    i += 2
                elif text.startswith("*/", i):
                    depth -= 1
                    i += 2
                else:
                    i += 1
            if depth:
                fail(
                    "syntax.parse.unterminated_comment",
                    "parse",
                    "unterminated block comment",
                    Span(start, len(text)),
                )
            continue
        start = i
        if text.startswith("::", i) or text.startswith("->", i) or text.startswith("=>", i):
            i += 2
        elif version == 1 and text.startswith("==", i):
            i += 2
        elif version == 1 and ch in "+-*<":
            i += 1
        elif ch in _PUNCT:
            i += 1
        elif ch == "-" and i + 1 < len(text) and text[i + 1].isdigit():
            i += 1
            while i < len(text) and text[i].isdigit(): i += 1
        elif ch.isdigit():
            while i < len(text) and text[i].isdigit(): i += 1
        elif ch.isalpha() or ch == "_":
            i += 1
            while i < len(text) and (text[i].isalnum() or text[i] in "_."):
                i += 1
        else:
            fail("syntax.parse.invalid_character", "parse", f"invalid character {ch!r}", Span(i, i + 1))
        if i - start > limits.token_bytes: _limit("token_bytes", limits.token_bytes, Span(start, i))
        out.append(_Token(text[start:i], start, i))
        if len(out) > limits.tokens: _limit("tokens", limits.tokens, Span(start, i))
    out.append(_Token("<eof>", len(text), len(text)))
    return tuple(out)


class _Parser:
    def __init__(self, tokens: tuple[_Token, ...], limits: Limits, *, version: int = 0):
        self.tokens, self.limits, self.version, self.i, self.depth, self.nodes = tokens, limits, version, 0, 0, 0

    @property
    def token(self) -> _Token: return self.tokens[self.i]

    def error(self, message: str, token: _Token | None = None) -> None:
        token = token or self.token
        fail("syntax.parse.unexpected_token", "parse", message, Span(token.start, token.end), token=token.text)

    def take(self, text: str | None = None) -> _Token:
        token = self.token
        if text is not None and token.text != text: self.error(f"expected {text!r}, found {token.text!r}")
        self.i += 1
        return token

    def peek(self, text: str) -> bool: return self.token.text == text

    def node(self, start: int, end: int) -> Span:
        self.nodes += 1
        if self.nodes > self.limits.ast_nodes: _limit("ast_nodes", self.limits.ast_nodes, Span(start, end))
        return Span(start, end)

    def enter(self) -> None:
        self.depth += 1
        if self.depth > self.limits.syntax_depth: _limit("syntax_depth", self.limits.syntax_depth, Span(self.token.start, self.token.end))

    def ident(self) -> _Token:
        token = self.token
        if not token.text or not (token.text[0].isalpha() or token.text[0] == "_") or "." in token.text or token.text in _KEYWORDS:
            self.error("expected identifier")
        return self.take()

    def lower_ident(self, role: str) -> _Token:
        token = self.ident()
        if not (token.text[0].islower() or token.text[0] == "_"):
            fail("syntax.parse.invalid_name", "parse", f"{role} must start with a lowercase letter or underscore",
                 Span(token.start, token.end), role=role, name=token.text)
        return token

    def upper_ident(self, role: str) -> _Token:
        token = self.ident()
        if not token.text[0].isupper():
            fail("syntax.parse.invalid_name", "parse", f"{role} must start with an uppercase letter",
                 Span(token.start, token.end), role=role, name=token.text)
        return token

    def path(self) -> tuple[str, Span]:
        first = self.ident(); parts = [first.text]; end = first.end
        while self.peek("::"):
            self.take(); nxt = self.ident(); parts.append(nxt.text); end = nxt.end
        return "::".join(parts), Span(first.start, end)

    def type_ref(self) -> TypeRef:
        name, span = self.path()
        if name == "Int": return IntType(self.node(span.start, span.end))
        if not name.split("::")[-1][0].isupper():
            fail("syntax.parse.invalid_name", "parse", "type name must start with an uppercase letter",
                 span, role="type", name=name)
        return NamedType(name, self.node(span.start, span.end))

    def binder(self) -> Binder:
        start = self.token.start; name = self.lower_ident("binder"); self.take(":"); typ = self.type_ref()
        return Binder(name.text, typ, self.node(start, typ.span.end))

    def comma_list(self, parse_item, close: str = ")") -> tuple:
        values = []
        if not self.peek(close):
            while True:
                values.append(parse_item())
                if not self.peek(","): break
                self.take(",")
        self.take(close)
        return tuple(values)

    def block(self) -> Block:
        start = self.take("{").start; bindings = []
        while self.peek("let"):
            bstart = self.take().start; binder = self.binder(); self.take("="); value = self.expr(); end = self.take(";").end
            bindings.append(LetBinding(binder, value, self.node(bstart, end)))
        result = self.expr()
        if self.peek(";"): self.take(";")
        end = self.take("}").end
        return Block(tuple(bindings), result, self.node(start, end))

    def expr(self):
        return self.binary_expr(0)

    def binary_expr(self, minimum: int):
        self.enter()
        try:
            result = self.atom()
            while self.peek("("):
                start = result.span.start; self.take("("); args = self.comma_list(self.expr)
                result = CallExpr(result, args, self.node(start, self.tokens[self.i - 1].end))
            if self.version == 1:
                precedence = {"==": 3, "<": 3, "+": 6, "-": 6, "*": 7}
                primitive = {"==": "int.equal", "<": "int.less", "+": "int.add", "-": "int.sub", "*": "int.mul"}
                while self.token.text in precedence and precedence[self.token.text] >= minimum:
                    operator = self.take()
                    right = self.binary_expr(precedence[operator.text] + 1)
                    result = PrimitiveExpr(
                        primitive[operator.text],
                        (result, right),
                        self.node(result.span.start, right.span.end),
                    )
            return result
        finally: self.depth -= 1

    def atom(self):
        token = self.token
        if self.version == 1 and token.text == "-":
            start = self.take("-").start
            literal = self.token
            if not literal.text.isdigit():
                self.error("unary minus currently requires an integer literal", literal)
            self.take()
            if len(literal.text) > self.limits.literal_digits:
                _limit("literal_digits", self.limits.literal_digits, Span(start, literal.end))
            return IntExpr(-parse_decimal(literal.text), self.node(start, literal.end))
        if token.text.lstrip("-").isdigit():
            self.take()
            if len(token.text.lstrip("-")) > self.limits.literal_digits: _limit("literal_digits", self.limits.literal_digits, Span(token.start, token.end))
            return IntExpr(parse_decimal(token.text), self.node(token.start, token.end))
        if self.peek("primitive"):
            start = self.take().start; name = self.take()
            primitives = {"int.add", "int.sub", "int.mul"}
            if self.version == 1:
                primitives |= {"int.equal", "int.less"}
            if name.text not in primitives:
                self.error("expected a supported integer primitive", name)
            self.take("("); args = self.comma_list(self.expr); end = self.tokens[self.i - 1].end
            return PrimitiveExpr(name.text, args, self.node(start, end))
        if self.peek("case"): return self.case_expr()
        if self.peek("("):
            self.take(); value = self.expr(); self.take(")"); return value
        name, span = self.path()
        if self.peek("("):
            self.take(); args = self.comma_list(self.expr); end = self.tokens[self.i - 1].end
            if len(name.split("::")) >= 2 and name.split("::")[-1][:1].isupper():
                return ConstructorExpr(name, args, self.node(span.start, end))
            if name.split("::")[-1][0].isupper():
                fail("syntax.parse.invalid_name", "parse", "constructor calls must be qualified through their type",
                     span, role="constructor", name=name)
            fn = LocalExpr(name, self.node(span.start, span.end)) if "::" not in name else GlobalExpr(name, self.node(span.start, span.end))
            return CallExpr(fn, args, self.node(span.start, end))
        if name.split("::")[-1][0].isupper():
            fail("syntax.parse.invalid_name", "parse", "value names must start with a lowercase letter or underscore",
                 span, role="value", name=name)
        return LocalExpr(name, self.node(span.start, span.end)) if "::" not in name else GlobalExpr(name, self.node(span.start, span.end))

    def case_expr(self) -> CaseExpr:
        start = self.take("case").start; scrutinee = self.expr(); self.take("->"); result_type = self.type_ref(); self.take("{")
        alternatives = []
        while not self.peek("}"):
            astart = self.token.start; ctor, ctor_span = self.path()
            if len(ctor.split("::")) < 2 or not ctor.split("::")[-1][0].isupper():
                fail("syntax.parse.invalid_name", "parse", "case constructor must be qualified through its type", ctor_span,
                     role="constructor", name=ctor)
            self.take("("); binders = self.comma_list(self.binder); self.take("=>"); body = self.block()
            if self.peek(";"): end = self.take().end
            else: end = body.span.end
            alternatives.append(CaseAlternative(ctor, binders, body, self.node(astart, end)))
        end = self.take("}").end
        return CaseExpr(scrutinee, result_type, tuple(alternatives), self.node(start, end))

    def import_decl(self) -> ImportDecl:
        start = self.take("import").start
        parts = [self.lower_ident("module component").text]
        while self.peek("::"):
            self.take()
            if self.peek("{"): break
            parts.append(self.lower_ident("module component").text)
        self.take("{"); names = self.comma_list(lambda: self.ident().text, "}"); end = self.take(";").end
        if not names: self.error("imports must select at least one name", self.tokens[self.i - 1])
        return ImportDecl("::".join(parts), names, self.node(start, end))

    def type_decl(self, public: bool, start: int) -> TypeDecl:
        self.take("type"); name = self.upper_ident("type"); self.take("{"); ctors = []
        while not self.peek("}"):
            cstart = self.token.start; ctor = self.upper_ident("constructor"); self.take("(")
            fields = self.comma_list(lambda: self.field())
            cend = self.take(";").end
            ctors.append(ConstructorDecl(ctor.text, fields, self.node(cstart, cend)))
        end = self.take("}").end
        return TypeDecl(name.text, tuple(ctors), public, self.node(start, end))

    def field(self) -> FieldDecl:
        start = self.token.start; binder = self.binder()
        return FieldDecl(binder.name, binder.type, self.node(start, binder.span.end))

    def fn_decl(self, public: bool, start: int) -> FunctionDecl:
        self.take("fn"); name = self.lower_ident("function"); self.take("("); params = self.comma_list(self.binder)
        self.take("->"); result = self.type_ref()
        body = self.function_clauses(params, result) if self.version == 1 and self.peek("|") else self.block()
        return FunctionDecl(name.text, params, result, body, public, self.node(start, body.span.end))

    def function_clauses(self, params: tuple[Binder, ...], result: TypeRef) -> Block:
        """Parse the v1 ordered exact-Int clause subset into ordinary cases.

        This first clause implementation deliberately canonicalizes immediately:
        public Syntax and Core need no clause-specific semantic node.
        """
        if len(params) != 1:
            self.error("function clauses currently require exactly one parameter")
        parameter = params[0]
        literals: set[int] = set()
        clauses: list[tuple[int | str, Block, Span]] = []
        catchall = False
        while self.peek("|"):
            clause_start = self.take("|").start
            token = self.token
            if token.text.lstrip("-").isdigit() or self.peek("-"):
                sign = -1 if self.peek("-") else 1
                if sign < 0:
                    self.take("-")
                    token = self.token
                    if not token.text.isdigit():
                        self.error("expected integer after '-' in function pattern", token)
                self.take()
                value = sign * parse_decimal(token.text)
                if value in literals:
                    self.error("duplicate integer function pattern", token)
                if catchall:
                    self.error("function clause is unreachable after a catch-all", token)
                literals.add(value)
                pattern: int | str = value
            else:
                name = self.lower_ident("pattern binder")
                if catchall:
                    self.error("function clause is unreachable after a catch-all", name)
                catchall = True
                pattern = name.text
            self.take("=>")
            body = self.block() if self.peek("{") else Block((), self.expr(), self.node(self.tokens[self.i - 1].start, self.tokens[self.i - 1].end))
            clauses.append((pattern, body, self.node(clause_start, body.span.end)))
        if not catchall:
            self.error("integer function clauses require a final binder or underscore catch-all")

        fallback: Block | None = None
        for pattern, body, span in reversed(clauses):
            if isinstance(pattern, str):
                if pattern not in ("_", parameter.name):
                    alias = Binder(pattern, parameter.type, span)
                    source = LocalExpr(parameter.name, span)
                    body = Block((LetBinding(alias, source, span), *body.bindings), body.result, span)
                fallback = body
                continue
            assert fallback is not None
            condition = PrimitiveExpr(
                "int.equal",
                (LocalExpr(parameter.name, span), IntExpr(pattern, span)),
                span,
            )
            case = CaseExpr(
                condition,
                result,
                (
                    CaseAlternative("Bool::False", (), fallback, span),
                    CaseAlternative("Bool::True", (), body, span),
                ),
                span,
            )
            fallback = Block((), case, span)
        assert fallback is not None
        return fallback

    def value_decl(self, public: bool, start: int) -> ValueDecl:
        self.take("value"); name = self.lower_ident("value"); self.take(":"); typ = self.type_ref(); body = self.block()
        return ValueDecl(name.text, typ, body, public, self.node(start, body.span.end))

    def module(self) -> Module:
        start = self.take("module").start
        first = self.lower_ident("module component"); parts = [first.text]
        while self.peek("::"):
            self.take(); parts.append(self.lower_ident("module component").text)
        name = "::".join(parts); self.take(";")
        imports = []
        while self.peek("import"): imports.append(self.import_decl())
        types, functions, values = [], [], []
        while not self.peek("<eof>"):
            dstart = self.token.start; public = False
            if self.peek("public"): public = True; self.take()
            if self.peek("type"): types.append(self.type_decl(public, dstart))
            elif self.peek("fn"): functions.append(self.fn_decl(public, dstart))
            elif self.peek("value"): values.append(self.value_decl(public, dstart))
            else: self.error("expected type, fn, or value declaration")
        end = self.token.end
        return Module(name, tuple(imports), tuple(types), tuple(functions), tuple(values), self.node(start, end))


def parse_source(source: str | bytes, limits: Limits | None = None) -> Module:
    """Parse the explicit-brace SlopH source profile using byte-offset spans."""
    actual = limits or Limits()
    module = _Parser(_lex(_source_bytes(source, actual), actual), actual).module()
    from sloph.syntax.validate import validate_syntax
    validate_syntax(module, actual)
    return module


def parse_source_v1(source: str | bytes, limits: Limits | None = None) -> Module:
    """Parse Source v1 without changing the experimental Source v0 contract."""
    actual = limits or Limits()
    module = _Parser(_lex(_source_bytes(source, actual), actual, version=1), actual, version=1).module()
    from sloph.syntax.validate import validate_syntax
    validate_syntax(module, actual, version=1)
    return module


__all__ = ["DiagnosticError", "parse_source", "parse_source_v1"]

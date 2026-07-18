from __future__ import annotations

from typing import Any

from sloph.core.diagnostics import Span, fail
from sloph.core.limits import Limits
from sloph.syntax._integer import decimal_digits
from sloph.syntax.model import *


def _bad(code: str, message: str, node: Any, **details: Any) -> None:
    fail(f"syntax.validate.{code}", "validate", message, getattr(node, "span", Span(0, 0)), **details)


def _ident(name: str) -> bool:
    return bool(name) and (name[0].isalpha() or name[0] == "_") and all(c.isalnum() or c == "_" for c in name) and name.isascii()


def _lower(name: str) -> bool: return _ident(name) and (name[0].islower() or name[0] == "_")
def _upper(name: str) -> bool: return _ident(name) and name[0].isupper()


_TARGET_SELECTOR_TYPES = {
    "compiler::target::platform": ("os", "arch"),
    "compiler::target::arch": "arch",
}
_TARGET_CONSTANT_TYPES = {
    "os::linux": "os",
    "os::darwin": "os",
    "arch::amd64": "arch",
    "arch::arm64": "arch",
}


def _target_pattern_type(pattern: TargetPattern):
    if isinstance(pattern, TargetConstantPattern):
        return _TARGET_CONSTANT_TYPES.get(pattern.name)
    if isinstance(pattern, TargetTuplePattern):
        return tuple(_target_pattern_type(item) for item in pattern.items)
    return None


def _target_pattern_key(pattern: TargetPattern):
    if isinstance(pattern, TargetConstantPattern):
        return pattern.name
    if isinstance(pattern, TargetTuplePattern):
        return tuple(_target_pattern_key(item) for item in pattern.items)
    return None


class _Validator:
    def __init__(self, limits: Limits, version: int = 0): self.limits, self.version, self.nodes, self.depth = limits, version, 0, 0

    def visit(self, node: Any) -> None:
        self.nodes += 1; self.depth += 1
        if self.nodes > self.limits.ast_nodes: _bad("limit_exceeded", f"ast_nodes limit exceeded (configured {self.limits.ast_nodes})", node, limit="ast_nodes", configured=self.limits.ast_nodes)
        if self.depth > self.limits.syntax_depth: _bad("limit_exceeded", f"syntax_depth limit exceeded (configured {self.limits.syntax_depth})", node, limit="syntax_depth", configured=self.limits.syntax_depth)
        if not isinstance(getattr(node, "span", None), Span) or node.span.start < 0 or node.span.end < node.span.start:
            _bad("invalid_span", "node span must contain ordered non-negative byte offsets", node)
        if node.span.end > self.limits.input_bytes:
            _bad("limit_exceeded", f"input_bytes limit exceeded (configured {self.limits.input_bytes})", node, limit="input_bytes", configured=self.limits.input_bytes)
        tuple_fields = {
            Module: ("imports", "types", "functions", "values"),
            ImportDecl: ("names",), ConstructorDecl: ("fields",),
            TargetTuplePattern: ("items",), ConditionalImportDecl: ("alternatives",),
            TypeDecl: ("constructors",), FunctionDecl: ("parameters",),
            CallExpr: ("arguments",), LambdaExpr: ("parameters",), ConstructorExpr: ("arguments",),
            PrimitiveExpr: ("arguments",), Block: ("bindings",),
            CaseAlternative: ("binders",), CaseExpr: ("alternatives",),
        }.get(type(node), ())
        for field in tuple_fields:
            if not isinstance(getattr(node, field, None), tuple):
                _bad("wrong_container", f"{field} must be a tuple", node, field=field)
        for field in ("name", "module", "constructor"):
            value = getattr(node, field, None)
            if isinstance(value, str) and any(
                len(part) > self.limits.token_bytes for part in value.split("::")
            ):
                _bad("limit_exceeded", f"token_bytes limit exceeded (configured {self.limits.token_bytes})", node, limit="token_bytes", configured=self.limits.token_bytes)
        try:
            method = getattr(self, f"v_{type(node).__name__}", None)
            if method is None: _bad("unknown_node", f"unknown syntax node {type(node).__name__}", node)
            method(node)
        finally: self.depth -= 1

    def typ(self, node: Any) -> None:
        if not isinstance(node, (IntType, NamedType, FunctionType, InferredType)): _bad("wrong_node", "expected source type", node)
        self.visit(node)

    def expr(self, node: Any) -> None:
        if not isinstance(node, (IntExpr, BytesExpr, LocalExpr, GlobalExpr, CallExpr, LambdaExpr, IfExpr, ConstructorExpr, PrimitiveExpr, CaseExpr)):
            _bad("wrong_node", "expected source expression", node)
        self.visit(node)

    def binder(self, node: Any) -> None:
        if not isinstance(node, Binder): _bad("wrong_node", "expected binder", node)
        self.visit(node)

    def block(self, node: Any) -> None:
        if not isinstance(node, Block): _bad("wrong_node", "expected block", node)
        self.visit(node)

    def v_IntType(self, node): pass
    def v_NamedType(self, node):
        parts = node.name.split("::")
        if not parts or not _upper(parts[-1]) or not all(_lower(x) for x in parts[:-1]): _bad("invalid_name", "named type must have lowercase module components and an uppercase type name", node, name=node.name)
    def v_FunctionType(self, node): self.typ(node.parameter); self.typ(node.result)
    def v_InferredType(self, node):
        if self.version == 0: _bad("wrong_node", "inferred types require Source v1", node)
    def v_Binder(self, node):
        if not _lower(node.name): _bad("invalid_name", "binder must start with lowercase or underscore", node, name=node.name)
        self.typ(node.type)
    def v_IntExpr(self, node):
        if not isinstance(node.value, int) or isinstance(node.value, bool): _bad("invalid_integer", "integer expression value must be an integer", node)
        if decimal_digits(node.value) > self.limits.literal_digits: _bad("limit_exceeded", f"literal_digits limit exceeded (configured {self.limits.literal_digits})", node, limit="literal_digits", configured=self.limits.literal_digits)
    def v_BytesExpr(self, node):
        if not isinstance(node.value, bytes): _bad("invalid_bytes", "byte literal value must be bytes", node)
        if len(node.value) > self.limits.input_bytes: _bad("limit_exceeded", f"input_bytes limit exceeded (configured {self.limits.input_bytes})", node, limit="input_bytes", configured=self.limits.input_bytes)
    def v_LocalExpr(self, node):
        if not _lower(node.name): _bad("invalid_name", "local name must start with lowercase or underscore", node, name=node.name)
    def v_GlobalExpr(self, node):
        parts = node.name.split("::")
        if len(parts) < 2 or not all(_lower(x) for x in parts): _bad("invalid_name", "global name must be qualified and lowercase", node, name=node.name)
    def v_CallExpr(self, node):
        if self.version == 0 and not isinstance(node.function, (LocalExpr, GlobalExpr)):
            _bad("dynamic_call", "calls must directly name a function", node)
        if self.version == 0 and not node.arguments:
            _bad("call_arity", "Source v0 calls require at least one argument", node)
        self.expr(node.function)
        for x in node.arguments: self.expr(x)
    def v_LambdaExpr(self, node):
        if self.version == 0: _bad("wrong_node", "lambda expressions require Source v1", node)
        for x in node.parameters: self.binder(x)
        self.typ(node.result_type); self.block(node.body)
    def v_IfExpr(self, node):
        if self.version == 0: _bad("wrong_node", "if expressions require Source v1", node)
        self.expr(node.condition); self.block(node.then_body); self.block(node.else_body)
    def v_ConstructorExpr(self, node):
        parts = node.constructor.split("::")
        if len(parts) < 2 or not _upper(parts[-1]) or not _upper(parts[-2]) or not all(_lower(x) for x in parts[:-2]): _bad("invalid_name", "constructor must be qualified through an uppercase type", node, name=node.constructor)
        for x in node.arguments: self.expr(x)
    def v_PrimitiveExpr(self, node):
        primitives = {"int.add", "int.sub", "int.mul"}
        if self.version == 1:
            primitives |= {
                "int.equal",
                "int.less",
                "bytes.length",
                "runtime.trap",
            }
        foreign = self.version == 1 and node.name.startswith("foreign.")
        if node.name not in primitives and not foreign: _bad("invalid_primitive", "unknown source primitive", node, name=node.name)
        expected = {
            "bytes.length": 1,
            "runtime.trap": 1,
        }.get(node.name, 2)
        if not foreign and len(node.arguments) != expected: _bad("primitive_arity", f"primitive requires exactly {expected} arguments", node, name=node.name)
        for x in node.arguments: self.expr(x)
    def v_LetBinding(self, node): self.binder(node.binder); self.expr(node.value)
    def v_Block(self, node):
        for x in node.bindings:
            if not isinstance(x, LetBinding): _bad("wrong_node", "block bindings must be LetBinding nodes", node)
            self.visit(x)
        self.expr(node.result)
    def v_CaseAlternative(self, node):
        parts = node.constructor.split("::")
        if len(parts) < 2 or not _upper(parts[-1]) or not _upper(parts[-2]) or not all(_lower(x) for x in parts[:-2]): _bad("invalid_name", "case constructor must be qualified through its type", node, name=node.constructor)
        for x in node.binders: self.binder(x)
        self.block(node.body)
    def v_CaseExpr(self, node):
        self.expr(node.scrutinee); self.typ(node.result_type)
        for x in node.alternatives:
            if not isinstance(x, CaseAlternative): _bad("wrong_node", "case alternatives must be CaseAlternative nodes", node)
            self.visit(x)
    def v_ImportDecl(self, node):
        if not node.names or not all(_lower(x) for x in node.module.split("::")): _bad("invalid_import", "import requires a lowercase module and selected names", node)
        if not all(_lower(x) or _upper(x) for x in node.names): _bad("invalid_import", "import selections must be unqualified source names", node)
        if any(len(x) > self.limits.token_bytes for x in node.names): _bad("limit_exceeded", f"token_bytes limit exceeded (configured {self.limits.token_bytes})", node, limit="token_bytes", configured=self.limits.token_bytes)
    def v_TargetConstantPattern(self, node):
        if node.name not in _TARGET_CONSTANT_TYPES:
            _bad("invalid_target", "unknown compiler-target constant", node, constant=node.name)
    def v_TargetTuplePattern(self, node):
        if not node.items:
            _bad("invalid_target", "compiler-target tuple pattern cannot be empty", node)
        for item in node.items:
            if not isinstance(item, (TargetConstantPattern, TargetTuplePattern)):
                _bad("wrong_node", "invalid compiler-target pattern", node)
            self.visit(item)
    def v_Availability(self, node):
        expected = _TARGET_SELECTOR_TYPES.get(node.selector)
        if expected is None or not isinstance(node.pattern, (TargetConstantPattern, TargetTuplePattern)):
            _bad("invalid_target", "invalid compiler-target availability", node)
        self.visit(node.pattern)
        if _target_pattern_type(node.pattern) != expected:
            _bad("target_type", "compiler-target pattern does not match selector type", node, selector=node.selector)
    def v_ConditionalImportAlternative(self, node):
        if not isinstance(node.pattern, (TargetConstantPattern, TargetTuplePattern)) or not isinstance(node.import_, ImportDecl):
            _bad("invalid_import", "invalid conditional import alternative", node)
        self.visit(node.pattern)
        self.visit(node.import_)
    def v_ConditionalImportDecl(self, node):
        if self.version != 1:
            _bad("invalid_target", "conditional imports require Source v1", node)
        expected = _TARGET_SELECTOR_TYPES.get(node.selector)
        if expected is None or not node.alternatives:
            _bad("invalid_target", "invalid conditional import selector", node)
        seen = set()
        for alternative in node.alternatives:
            if not isinstance(alternative, ConditionalImportAlternative):
                _bad("invalid_import", "invalid or duplicate conditional import alternative", node)
            self.visit(alternative)
            key = _target_pattern_key(alternative.pattern)
            if key in seen:
                _bad("invalid_import", "invalid or duplicate conditional import alternative", node)
            seen.add(key)
            if _target_pattern_type(alternative.pattern) != expected:
                _bad("target_type", "compiler-target pattern does not match selector type", alternative, selector=node.selector)
    def v_FieldDecl(self, node):
        if not _lower(node.name): _bad("invalid_name", "field must start with lowercase or underscore", node, name=node.name)
        self.typ(node.type)
    def v_ConstructorDecl(self, node):
        if not _upper(node.name): _bad("invalid_name", "constructor must start with uppercase", node, name=node.name)
        for x in node.fields:
            if not isinstance(x, FieldDecl): _bad("wrong_node", "constructor fields must be FieldDecl nodes", node)
            self.visit(x)
    def v_TypeDecl(self, node):
        if not isinstance(node.public, bool) or not _upper(node.name): _bad("invalid_declaration", "type must have boolean visibility and uppercase name", node)
        for x in node.constructors:
            if not isinstance(x, ConstructorDecl): _bad("wrong_node", "type constructors must be ConstructorDecl nodes", node)
            self.visit(x)
    def v_FunctionDecl(self, node):
        if not isinstance(node.public, bool) or not _lower(node.name): _bad("invalid_declaration", "function must have boolean visibility and lowercase name", node)
        if self.version == 0 and not node.parameters: _bad("function_arity", "Source v0 functions require at least one parameter", node)
        for x in node.parameters:
            if isinstance(x.type, InferredType): _bad("missing_type", "function parameters require explicit types", x)
            self.binder(x)
        self.typ(node.result_type); self.block(node.body)
    def v_ValueDecl(self, node):
        if not isinstance(node.public, bool) or not _lower(node.name): _bad("invalid_declaration", "value must have boolean visibility and lowercase name", node)
        self.typ(node.type); self.block(node.value)
    def v_Module(self, node):
        if not all(_lower(x) for x in node.name.split("::")): _bad("invalid_name", "module components must start with lowercase or underscore", node, name=node.name)
        if node.availability is not None:
            if self.version != 1 or not isinstance(node.availability, Availability): _bad("invalid_target", "module availability requires Source v1", node)
            self.visit(node.availability)
        groups = ((node.imports, (ImportDecl, ConditionalImportDecl)), (node.types, TypeDecl), (node.functions, FunctionDecl), (node.values, ValueDecl))
        for values, expected in groups:
            for value in values:
                if not isinstance(value, expected): _bad("wrong_node", "module contains an invalid declaration node", node)
                self.visit(value)


def validate_syntax(module: Module, limits: Limits | None = None, *, version: int = 0) -> None:
    """Validate an in-memory public Syntax AST without resolving names or types."""
    if not isinstance(module, Module): raise TypeError("module must be a syntax Module")
    if version not in (0, 1):
        raise ValueError("syntax version must be 0 or 1")
    _Validator(limits or Limits(), version).visit(module)


__all__ = ["validate_syntax"]

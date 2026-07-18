from __future__ import annotations

from dataclasses import dataclass
from typing import TypeAlias

from sloph.core.diagnostics import Span, UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class IntType:
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class NamedType:
    name: str
    span: Span = UNKNOWN_SPAN


TypeRef: TypeAlias = IntType | NamedType


@dataclass(frozen=True, slots=True)
class Binder:
    name: str
    type: TypeRef
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class IntExpr:
    value: int
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class BytesExpr:
    value: bytes
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class LocalExpr:
    name: str
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class GlobalExpr:
    name: str
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class CallExpr:
    function: "Expr"
    arguments: tuple["Expr", ...]
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class ConstructorExpr:
    constructor: str
    arguments: tuple["Expr", ...]
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class PrimitiveExpr:
    name: str
    arguments: tuple["Expr", ...]
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class LetBinding:
    binder: Binder
    value: "Expr"
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class Block:
    bindings: tuple[LetBinding, ...]
    result: "Expr"
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class CaseAlternative:
    constructor: str
    binders: tuple[Binder, ...]
    body: Block
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class CaseExpr:
    scrutinee: "Expr"
    result_type: TypeRef
    alternatives: tuple[CaseAlternative, ...]
    span: Span = UNKNOWN_SPAN


Expr: TypeAlias = (
    IntExpr | BytesExpr | LocalExpr | GlobalExpr | CallExpr | ConstructorExpr | PrimitiveExpr | CaseExpr
)


@dataclass(frozen=True, slots=True)
class ImportDecl:
    module: str
    names: tuple[str, ...]
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class FieldDecl:
    name: str
    type: TypeRef
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class ConstructorDecl:
    name: str
    fields: tuple[FieldDecl, ...]
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class TypeDecl:
    name: str
    constructors: tuple[ConstructorDecl, ...]
    public: bool = False
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class FunctionDecl:
    name: str
    parameters: tuple[Binder, ...]
    result_type: TypeRef
    body: Block
    public: bool = False
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class ValueDecl:
    name: str
    type: TypeRef
    value: Block
    public: bool = False
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class Module:
    name: str
    imports: tuple[ImportDecl, ...]
    types: tuple[TypeDecl, ...]
    functions: tuple[FunctionDecl, ...]
    values: tuple[ValueDecl, ...]
    span: Span = UNKNOWN_SPAN

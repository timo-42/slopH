from __future__ import annotations

from dataclasses import dataclass
from typing import TypeAlias

from sloph.core.diagnostics import Span, UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class IntType:
    pass


INT = IntType()


@dataclass(frozen=True, slots=True)
class NamedType:
    name: str


@dataclass(frozen=True, slots=True)
class FunctionType:
    parameter: "CoreType"
    result: "CoreType"


CoreType: TypeAlias = IntType | NamedType | FunctionType


@dataclass(frozen=True, slots=True)
class Binder:
    name: str
    type: CoreType
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
class LamExpr:
    binder: Binder
    body: "Expr"
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class AppExpr:
    function: "Expr"
    argument: "Expr"
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class LetExpr:
    binder: Binder
    value: "Expr"
    body: "Expr"
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class PrimExpr:
    name: str
    arguments: tuple["Expr", ...]
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class ConExpr:
    constructor: str
    fields: tuple["Expr", ...]
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class Alternative:
    constructor: str
    binders: tuple[Binder, ...]
    body: "Expr"
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class CaseExpr:
    scrutinee: "Expr"
    result_type: CoreType
    alternatives: tuple[Alternative, ...]
    span: Span = UNKNOWN_SPAN


Expr: TypeAlias = (
    IntExpr
    | BytesExpr
    | LocalExpr
    | GlobalExpr
    | LamExpr
    | AppExpr
    | LetExpr
    | PrimExpr
    | ConExpr
    | CaseExpr
)


@dataclass(frozen=True, slots=True)
class FieldDecl:
    name: str
    type: CoreType
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class ConstructorDecl:
    name: str
    fields: tuple[FieldDecl, ...]
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class EnumDecl:
    name: str
    constructors: tuple[ConstructorDecl, ...]
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class Definition:
    name: str
    type: CoreType
    value: Expr
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class CoreUnit:
    version: int
    types: tuple[EnumDecl, ...]
    definitions: tuple[Definition, ...]
    span: Span = UNKNOWN_SPAN

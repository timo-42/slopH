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


@dataclass(frozen=True, slots=True)
class AppliedType:
    constructor: str
    arguments: tuple["TypeRef", ...]
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class FunctionType:
    parameter: "TypeRef"
    result: "TypeRef"
    span: Span = UNKNOWN_SPAN
    mode: str = "own"


@dataclass(frozen=True, slots=True)
class InferredType:
    span: Span = UNKNOWN_SPAN


TypeRef: TypeAlias = IntType | NamedType | AppliedType | FunctionType | InferredType


@dataclass(frozen=True, slots=True)
class Binder:
    name: str
    type: TypeRef
    span: Span = UNKNOWN_SPAN
    mode: str = "own"


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
    type_arguments: tuple[TypeRef, ...] = ()


@dataclass(frozen=True, slots=True)
class BinaryExpr:
    operator: str
    left: "Expr"
    right: "Expr"
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class LambdaExpr:
    parameters: tuple[Binder, ...]
    result_type: TypeRef
    body: "Block"
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class IfExpr:
    condition: "Expr"
    then_body: "Block"
    else_body: "Block"
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class ConstructorExpr:
    constructor: str
    arguments: tuple["Expr", ...]
    span: Span = UNKNOWN_SPAN
    type_arguments: tuple[TypeRef, ...] = ()


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
class DeferCall:
    call: CallExpr
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class Block:
    bindings: tuple[LetBinding | DeferCall, ...]
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
    IntExpr | BytesExpr | LocalExpr | GlobalExpr | CallExpr | BinaryExpr | LambdaExpr | IfExpr | ConstructorExpr | PrimitiveExpr | CaseExpr
)


@dataclass(frozen=True, slots=True)
class ImportDecl:
    module: str
    names: tuple[str, ...]
    span: Span = UNKNOWN_SPAN
    public: bool = False


@dataclass(frozen=True, slots=True)
class TargetConstantPattern:
    name: str
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class TargetTuplePattern:
    items: tuple["TargetPattern", ...]
    span: Span = UNKNOWN_SPAN


TargetPattern: TypeAlias = TargetConstantPattern | TargetTuplePattern


@dataclass(frozen=True, slots=True)
class Availability:
    selector: str
    pattern: TargetPattern
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class ConditionalImportAlternative:
    pattern: TargetPattern
    import_: ImportDecl
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class ConditionalImportDecl:
    selector: str
    alternatives: tuple[ConditionalImportAlternative, ...]
    span: Span = UNKNOWN_SPAN


Import: TypeAlias = ImportDecl | ConditionalImportDecl


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
    type_parameters: tuple[str, ...] = ()
    owned: bool = False


@dataclass(frozen=True, slots=True)
class IntrinsicTypeDecl:
    name: str
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
    type_parameters: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class IntrinsicFunctionDecl:
    name: str
    parameters: tuple[Binder, ...]
    result_type: TypeRef
    intrinsic: str
    public: bool = False
    span: Span = UNKNOWN_SPAN


@dataclass(frozen=True, slots=True)
class ForeignFunctionDecl:
    name: str
    parameters: tuple[Binder, ...]
    result_type: TypeRef
    binding: str
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
    imports: tuple[Import, ...]
    types: tuple[TypeDecl | IntrinsicTypeDecl, ...]
    functions: tuple[FunctionDecl | IntrinsicFunctionDecl | ForeignFunctionDecl, ...]
    values: tuple[ValueDecl, ...]
    span: Span = UNKNOWN_SPAN
    availability: Availability | None = None

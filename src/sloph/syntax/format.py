from __future__ import annotations

from sloph.core.diagnostics import fail
from sloph.core.limits import Limits
from sloph.syntax._integer import format_decimal
from sloph.syntax.model import (
    Block, BytesExpr, CallExpr, CaseExpr, ConstructorExpr, Expr, FunctionType, GlobalExpr, IntExpr,
    IntType, LocalExpr, Module, NamedType, PrimitiveExpr, TypeRef,
)


def _type(value: TypeRef) -> str:
    if isinstance(value, IntType): return "Int"
    if isinstance(value, NamedType): return value.name
    if isinstance(value, FunctionType): return f"fn({_type(value.parameter)}) -> {_type(value.result)}"
    raise TypeError(f"unknown syntax type: {type(value).__name__}")


def _expr(value: Expr, indent: int) -> str:
    if isinstance(value, IntExpr): return format_decimal(value.value)
    if isinstance(value, BytesExpr): return _bytes(value.value)
    if isinstance(value, (LocalExpr, GlobalExpr)): return value.name
    if isinstance(value, CallExpr):
        return f"{_expr(value.function, indent)}({', '.join(_expr(a, indent) for a in value.arguments)})"
    if isinstance(value, ConstructorExpr):
        return f"{value.constructor}({', '.join(_expr(a, indent) for a in value.arguments)})"
    if isinstance(value, PrimitiveExpr):
        return f"primitive {value.name}({', '.join(_expr(a, indent) for a in value.arguments)})"
    if isinstance(value, CaseExpr):
        pad = " " * indent
        lines = [f"case {_expr(value.scrutinee, indent)} -> {_type(value.result_type)} {{"]
        for alt in value.alternatives:
            binders = ", ".join(f"{b.name}: {_type(b.type)}" for b in alt.binders)
            body = _block(alt.body, indent + 2)
            lines.append(f"{pad}  {alt.constructor}({binders}) => {body}")
        lines.append(f"{pad}}}")
        return "\n".join(lines)
    raise TypeError(f"unknown syntax expression: {type(value).__name__}")


def _bytes(value: bytes) -> str:
    simple = {0: "\\0", 9: "\\t", 10: "\\n", 13: "\\r", 34: '\\"', 92: "\\\\"}
    parts = ['"']
    for byte in value:
        if byte in simple:
            parts.append(simple[byte])
        elif 32 <= byte <= 126:
            parts.append(chr(byte))
        else:
            parts.append(f"\\x{byte:02x}")
    parts.append('"')
    return "".join(parts)


def _block(value: Block, indent: int) -> str:
    pad = " " * indent
    lines = ["{"]
    for binding in value.bindings:
        b = binding.binder
        rendered = _expr(binding.value, indent + 2)
        lines.append(f"{pad}  let {b.name}: {_type(b.type)} = {rendered};")
    lines.append(f"{pad}  {_expr(value.result, indent + 2)}")
    lines.append(f"{pad}}}")
    return "\n".join(lines)


def format_source(
    module: Module, limits: Limits | None = None, *, version: int = 0
) -> str:
    """Return the deterministic canonical source spelling."""
    if not isinstance(module, Module): raise TypeError("module must be a syntax Module")
    actual = limits or Limits()
    from sloph.syntax.validate import validate_syntax
    validate_syntax(module, actual, version=version)
    lines = [f"module {module.name};"]
    if module.imports:
        lines.append("")
        for item in module.imports:
            lines.append(f"import {item.module}::{{{', '.join(item.names)}}};")
    declarations: list[str] = []
    for declaration in module.types:
        prefix = "public " if declaration.public else ""
        body = [f"{prefix}type {declaration.name} {{"]
        for constructor in declaration.constructors:
            fields = ", ".join(f"{f.name}: {_type(f.type)}" for f in constructor.fields)
            body.append(f"  {constructor.name}({fields});")
        body.append("}")
        declarations.append("\n".join(body))
    for declaration in module.functions:
        prefix = "public " if declaration.public else ""
        params = ", ".join(f"{p.name}: {_type(p.type)}" for p in declaration.parameters)
        declarations.append(
            f"{prefix}fn {declaration.name}({params}) -> {_type(declaration.result_type)} "
            + _block(declaration.body, 0)
        )
    for declaration in module.values:
        prefix = "public " if declaration.public else ""
        declarations.append(f"{prefix}value {declaration.name}: {_type(declaration.type)} " + _block(declaration.value, 0))
    if declarations:
        lines.extend(["", "\n\n".join(declarations)])
    rendered = "\n".join(lines) + "\n"
    maximum = actual.output_bytes
    if len(rendered.encode("ascii")) > maximum:
        fail("syntax.print.limit_exceeded", "print", f"output_bytes limit exceeded (configured {maximum})",
             limit="output_bytes", configured=maximum)
    return rendered


__all__ = ["format_source"]

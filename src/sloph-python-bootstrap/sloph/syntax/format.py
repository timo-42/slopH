from __future__ import annotations

from sloph.core.diagnostics import fail
from sloph.core.limits import Limits
from sloph.syntax._integer import format_decimal
from sloph.syntax.model import (
    AppliedType, Block, BytesExpr, CallExpr, CaseExpr, ConditionalImportDecl, ConstructorExpr, Expr, FunctionType, GlobalExpr, IfExpr, InferredType, IntExpr, LambdaExpr,
    IntType, LocalExpr, Module, NamedType, PrimitiveExpr, TargetConstantPattern,
    TargetPattern, TargetTuplePattern, TypeRef,
)


def _type(value: TypeRef) -> str:
    if isinstance(value, IntType): return "Int"
    if isinstance(value, NamedType): return value.name
    if isinstance(value, AppliedType): return f"{value.constructor}[{', '.join(_type(item) for item in value.arguments)}]"
    if isinstance(value, FunctionType): return f"fn({_type(value.parameter)}) -> {_type(value.result)}"
    if isinstance(value, InferredType): return "_"
    raise TypeError(f"unknown syntax type: {type(value).__name__}")


def _target_pattern(pattern: TargetPattern) -> str:
    if isinstance(pattern, TargetConstantPattern):
        return pattern.name
    if isinstance(pattern, TargetTuplePattern):
        return f"({', '.join(_target_pattern(item) for item in pattern.items)})"
    raise TypeError(f"unknown target pattern: {type(pattern).__name__}")


def _expr(value: Expr, indent: int) -> str:
    if isinstance(value, IntExpr): return format_decimal(value.value)
    if isinstance(value, BytesExpr): return _bytes(value.value)
    if isinstance(value, (LocalExpr, GlobalExpr)): return value.name
    if isinstance(value, CallExpr):
        types = f"[{', '.join(_type(a) for a in value.type_arguments)}]" if value.type_arguments else ""
        return f"{_expr(value.function, indent)}{types}({', '.join(_expr(a, indent) for a in value.arguments)})"
    if isinstance(value, LambdaExpr):
        parameters = ", ".join(f"{item.name}: {_type(item.type)}" for item in value.parameters)
        return f"fn({parameters}) -> {_type(value.result_type)} " + _block(value.body, indent)
    if isinstance(value, IfExpr):
        return f"if {_expr(value.condition, indent)} {_block(value.then_body, indent)} else {_block(value.else_body, indent)}"
    if isinstance(value, ConstructorExpr):
        types = f"[{', '.join(_type(a) for a in value.type_arguments)}]" if value.type_arguments else ""
        return f"{value.constructor}{types}({', '.join(_expr(a, indent) for a in value.arguments)})"
    if isinstance(value, PrimitiveExpr):
        return f"primitive {value.name}({', '.join(_expr(a, indent) for a in value.arguments)})"
    if isinstance(value, CaseExpr):
        pad = " " * indent
        lines = [f"case {_expr(value.scrutinee, indent)} -> {_type(value.result_type)} {{"]
        for alt in value.alternatives:
            binders = ", ".join(
                b.name if isinstance(b.type, InferredType) else f"{b.name}: {_type(b.type)}"
                for b in alt.binders
            )
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
        annotation = "" if isinstance(b.type, InferredType) else f": {_type(b.type)}"
        lines.append(f"{pad}  let {b.name}{annotation} = {rendered};")
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
    availability = ""
    if module.availability is not None:
        availability = f" when {module.availability.selector} is {_target_pattern(module.availability.pattern)}"
    lines = [f"module {module.name}{availability};"]
    if module.imports:
        lines.append("")
        for item in module.imports:
            if isinstance(item, ConditionalImportDecl):
                lines.append(f"import case {item.selector} {{")
                for alternative in item.alternatives:
                    selected = alternative.import_
                    lines.append(
                        f"  {_target_pattern(alternative.pattern)} => "
                        f"{selected.module}::{{{', '.join(selected.names)}}};"
                    )
                lines.append("}")
            else:
                lines.append(f"import {item.module}::{{{', '.join(item.names)}}};")
    declarations: list[str] = []
    for declaration in module.types:
        prefix = "public " if declaration.public else ""
        type_parameters = f"[{', '.join(declaration.type_parameters)}]" if declaration.type_parameters else ""
        body = [f"{prefix}type {declaration.name}{type_parameters} {{"]
        for constructor in declaration.constructors:
            fields = ", ".join(f"{f.name}: {_type(f.type)}" for f in constructor.fields)
            body.append(f"  {constructor.name}({fields});")
        body.append("}")
        declarations.append("\n".join(body))
    for declaration in module.functions:
        prefix = "public " if declaration.public else ""
        type_parameters = f"[{', '.join(declaration.type_parameters)}]" if declaration.type_parameters else ""
        params = ", ".join(f"{p.name}: {_type(p.type)}" for p in declaration.parameters)
        declarations.append(
            f"{prefix}fn {declaration.name}{type_parameters}({params}) -> {_type(declaration.result_type)} "
            + _block(declaration.body, 0)
        )
    for declaration in module.values:
        prefix = "public " if declaration.public else ""
        keyword = "const" if version == 1 else "value"
        declarations.append(f"{prefix}{keyword} {declaration.name}: {_type(declaration.type)} " + _block(declaration.value, 0))
    if declarations:
        lines.extend(["", "\n\n".join(declarations)])
    rendered = "\n".join(lines) + "\n"
    maximum = actual.output_bytes
    if len(rendered.encode("ascii")) > maximum:
        fail("syntax.print.limit_exceeded", "print", f"output_bytes limit exceeded (configured {maximum})",
             limit="output_bytes", configured=maximum)
    return rendered


__all__ = ["format_source"]

from __future__ import annotations

import json
from typing import Any, Callable

from sloph.core.diagnostics import Span, fail
from sloph.core.limits import Limits
from sloph.syntax._integer import format_decimal, parse_decimal
from sloph.syntax.model import *


def _span(value: Span) -> dict[str, int]: return {"start": value.start, "end": value.end}


def _encode(value: Any) -> dict[str, Any]:
    common = {"kind": type(value).__name__, "span": _span(value.span)}
    if isinstance(value, IntType): return common
    if isinstance(value, NamedType): return common | {"name": value.name}
    if isinstance(value, AppliedType): return common | {"constructor": value.constructor, "arguments": [_encode(x) for x in value.arguments]}
    if isinstance(value, FunctionType):
        result = common | {"parameter": _encode(value.parameter), "result": _encode(value.result)}
        if value.mode != "own": result["mode"] = value.mode
        return result
    if isinstance(value, InferredType): return common
    if isinstance(value, Binder):
        result = common | {"name": value.name, "type": _encode(value.type)}
        if value.mode != "own": result["mode"] = value.mode
        return result
    if isinstance(value, IntExpr): return common | {"value": format_decimal(value.value)}
    if isinstance(value, BytesExpr): return common | {"hex": value.value.hex()}
    if isinstance(value, (LocalExpr, GlobalExpr)): return common | {"name": value.name}
    if isinstance(value, CallExpr): return common | {"function": _encode(value.function), "arguments": [_encode(x) for x in value.arguments], "type_arguments": [_encode(x) for x in value.type_arguments]}
    if isinstance(value, BinaryExpr): return common | {"operator": value.operator, "left": _encode(value.left), "right": _encode(value.right)}
    if isinstance(value, LambdaExpr): return common | {"parameters": [_encode(x) for x in value.parameters], "result_type": _encode(value.result_type), "body": _encode(value.body)}
    if isinstance(value, IfExpr): return common | {"condition": _encode(value.condition), "then_body": _encode(value.then_body), "else_body": _encode(value.else_body)}
    if isinstance(value, ConstructorExpr): return common | {"constructor": value.constructor, "arguments": [_encode(x) for x in value.arguments], "type_arguments": [_encode(x) for x in value.type_arguments]}
    if isinstance(value, PrimitiveExpr): return common | {"name": value.name, "arguments": [_encode(x) for x in value.arguments]}
    if isinstance(value, LetBinding): return common | {"binder": _encode(value.binder), "value": _encode(value.value)}
    if isinstance(value, DeferCall): return common | {"call": _encode(value.call)}
    if isinstance(value, Block): return common | {"bindings": [_encode(x) for x in value.bindings], "result": _encode(value.result)}
    if isinstance(value, CaseAlternative): return common | {"constructor": value.constructor, "binders": [_encode(x) for x in value.binders], "body": _encode(value.body)}
    if isinstance(value, CaseExpr): return common | {"scrutinee": _encode(value.scrutinee), "result_type": _encode(value.result_type), "alternatives": [_encode(x) for x in value.alternatives]}
    if isinstance(value, ImportDecl):
        result = common | {"module": value.module, "names": list(value.names)}
        if value.public: result["public"] = True
        return result
    if isinstance(value, TargetConstantPattern): return common | {"name": value.name}
    if isinstance(value, TargetTuplePattern): return common | {"items": [_encode(x) for x in value.items]}
    if isinstance(value, Availability): return common | {"selector": value.selector, "pattern": _encode(value.pattern)}
    if isinstance(value, ConditionalImportAlternative): return common | {"pattern": _encode(value.pattern), "import": _encode(value.import_)}
    if isinstance(value, ConditionalImportDecl): return common | {"selector": value.selector, "alternatives": [_encode(x) for x in value.alternatives]}
    if isinstance(value, FieldDecl): return common | {"name": value.name, "type": _encode(value.type)}
    if isinstance(value, ConstructorDecl): return common | {"name": value.name, "fields": [_encode(x) for x in value.fields]}
    if isinstance(value, TypeDecl):
        result = common | {"name": value.name, "constructors": [_encode(x) for x in value.constructors], "public": value.public, "type_parameters": list(value.type_parameters)}
        if value.owned: result["owned"] = True
        return result
    if isinstance(value, IntrinsicTypeDecl): return common | {"name": value.name, "public": value.public}
    if isinstance(value, FunctionDecl): return common | {"name": value.name, "parameters": [_encode(x) for x in value.parameters], "result_type": _encode(value.result_type), "body": _encode(value.body), "public": value.public, "type_parameters": list(value.type_parameters)}
    if isinstance(value, IntrinsicFunctionDecl): return common | {"name": value.name, "parameters": [_encode(x) for x in value.parameters], "result_type": _encode(value.result_type), "intrinsic": value.intrinsic, "public": value.public}
    if isinstance(value, ForeignFunctionDecl): return common | {"name": value.name, "parameters": [_encode(x) for x in value.parameters], "result_type": _encode(value.result_type), "binding": value.binding, "public": value.public}
    if isinstance(value, ValueDecl): return common | {"name": value.name, "type": _encode(value.type), "value": _encode(value.value), "public": value.public}
    if isinstance(value, Module):
        result = common | {"name": value.name, "imports": [_encode(x) for x in value.imports], "types": [_encode(x) for x in value.types], "functions": [_encode(x) for x in value.functions], "values": [_encode(x) for x in value.values]}
        if value.availability is not None:
            result["availability"] = _encode(value.availability)
        return result
    raise TypeError(f"unknown syntax node: {type(value).__name__}")


def syntax_to_json(
    module: Module, limits: Limits | None = None, *, version: int = 0
) -> str:
    if not isinstance(module, Module): raise TypeError("module must be a syntax Module")
    actual = limits or Limits()
    from sloph.syntax.validate import validate_syntax
    validate_syntax(module, actual, version=version)
    rendered = json.dumps({"schema": "sloph.syntax", "version": version, "module": _encode(module)}, ensure_ascii=True, sort_keys=True, separators=(",", ":")) + "\n"
    maximum = actual.output_bytes
    if len(rendered.encode("ascii")) > maximum:
        fail("syntax.json.limit_exceeded", "json", f"output_bytes limit exceeded (configured {maximum})", limit="output_bytes", configured=maximum)
    return rendered


class _Decoder:
    def __init__(self, limits: Limits): self.limits, self.nodes, self.depth = limits, 0, 0

    def bad(self, message: str, **details: Any) -> None:
        fail("syntax.json.invalid", "json", message, **details)

    def obj(self, value: Any, keys: set[str], kind: str | None = None) -> dict[str, Any]:
        if not isinstance(value, dict): self.bad("expected JSON object")
        if set(value) != keys: self.bad("object has missing or unknown fields", expected=sorted(keys), actual=sorted(value))
        if kind is not None and value.get("kind") != kind: self.bad(f"expected node kind {kind!r}", actual=value.get("kind"))
        return value

    def string(self, value: Any, field: str) -> str:
        if not isinstance(value, str) or not value: self.bad(f"{field} must be a non-empty string")
        try: value.encode("ascii")
        except UnicodeEncodeError: self.bad(f"{field} must contain ASCII only")
        if len(value) > self.limits.token_bytes: fail("syntax.json.limit_exceeded", "json", f"token_bytes limit exceeded (configured {self.limits.token_bytes})", limit="token_bytes", configured=self.limits.token_bytes)
        return value

    def boolean(self, value: Any, field: str) -> bool:
        if not isinstance(value, bool): self.bad(f"{field} must be a boolean")
        return value

    def span(self, value: Any) -> Span:
        obj = self.obj(value, {"start", "end"})
        if not all(isinstance(obj[x], int) and not isinstance(obj[x], bool) and obj[x] >= 0 for x in ("start", "end")) or obj["end"] < obj["start"]:
            self.bad("span must contain ordered non-negative integer offsets")
        return Span(obj["start"], obj["end"])

    def array(self, value: Any, decode: Callable[[Any], Any]) -> tuple:
        if not isinstance(value, list): self.bad("expected JSON array")
        return tuple(decode(x) for x in value)

    def node(self, value: Any):
        self.depth += 1; self.nodes += 1
        if self.depth > self.limits.syntax_depth: fail("syntax.json.limit_exceeded", "json", f"syntax_depth limit exceeded (configured {self.limits.syntax_depth})", limit="syntax_depth", configured=self.limits.syntax_depth)
        if self.nodes > self.limits.ast_nodes: fail("syntax.json.limit_exceeded", "json", f"ast_nodes limit exceeded (configured {self.limits.ast_nodes})", limit="ast_nodes", configured=self.limits.ast_nodes)
        try:
            if not isinstance(value, dict) or not isinstance(value.get("kind"), str): self.bad("node must have a string kind")
            kind = value["kind"]
            fields: dict[str, set[str]] = {
                "IntType": set(), "NamedType": {"name"}, "AppliedType": {"constructor", "arguments"}, "FunctionType": {"parameter", "result", "mode"}, "InferredType": set(), "Binder": {"name", "type", "mode"}, "IntExpr": {"value"}, "BytesExpr": {"hex"},
                "LocalExpr": {"name"}, "GlobalExpr": {"name"}, "CallExpr": {"function", "arguments", "type_arguments"}, "BinaryExpr": {"operator", "left", "right"}, "LambdaExpr": {"parameters", "result_type", "body"}, "IfExpr": {"condition", "then_body", "else_body"},
                "ConstructorExpr": {"constructor", "arguments", "type_arguments"}, "PrimitiveExpr": {"name", "arguments"},
                "LetBinding": {"binder", "value"}, "DeferCall": {"call"}, "Block": {"bindings", "result"},
                "CaseAlternative": {"constructor", "binders", "body"}, "CaseExpr": {"scrutinee", "result_type", "alternatives"},
                "ImportDecl": {"module", "names"}, "FieldDecl": {"name", "type"},
                "TargetConstantPattern": {"name"}, "TargetTuplePattern": {"items"},
                "Availability": {"selector", "pattern"},
                "ConditionalImportAlternative": {"pattern", "import"},
                "ConditionalImportDecl": {"selector", "alternatives"},
                "ConstructorDecl": {"name", "fields"}, "TypeDecl": {"name", "constructors", "public", "type_parameters", "owned"},
                "IntrinsicTypeDecl": {"name", "public"},
                "FunctionDecl": {"name", "parameters", "result_type", "body", "public", "type_parameters"},
                "IntrinsicFunctionDecl": {"name", "parameters", "result_type", "intrinsic", "public"},
                "ForeignFunctionDecl": {"name", "parameters", "result_type", "binding", "public"},
                "ValueDecl": {"name", "type", "value", "public"},
                "Module": {"name", "imports", "types", "functions", "values", "availability"},
            }
            if kind not in fields: self.bad("unknown node kind", kind=kind)
            expected_fields = fields[kind]
            if kind == "Module" and "availability" not in value:
                expected_fields = expected_fields - {"availability"}
            if kind == "ImportDecl" and "public" in value:
                expected_fields = expected_fields | {"public"}
            if kind in {"FunctionType", "Binder"} and "mode" not in value:
                expected_fields = expected_fields - {"mode"}
            if kind == "TypeDecl" and "owned" not in value:
                expected_fields = expected_fields - {"owned"}
            obj = self.obj(value, {"kind", "span"} | expected_fields, kind); span = self.span(obj["span"])
            s, n, a = self.string, self.node, self.array
            if kind == "IntType": return IntType(span)
            if kind == "NamedType": return NamedType(s(obj["name"], "name"), span)
            if kind == "AppliedType": return AppliedType(s(obj["constructor"], "constructor"), a(obj["arguments"], n), span)
            if kind == "FunctionType": return FunctionType(n(obj["parameter"]), n(obj["result"]), span, s(obj["mode"], "mode") if "mode" in obj else "own")
            if kind == "InferredType": return InferredType(span)
            if kind == "Binder": return Binder(s(obj["name"], "name"), n(obj["type"]), span, s(obj["mode"], "mode") if "mode" in obj else "own")
            if kind == "IntExpr":
                raw = s(obj["value"], "value")
                if raw == "-0" or not (raw.isdigit() or (raw.startswith("-") and raw[1:].isdigit())): self.bad("integer value must use canonical decimal spelling")
                if len(raw.lstrip("-")) > self.limits.literal_digits: fail("syntax.json.limit_exceeded", "json", f"literal_digits limit exceeded (configured {self.limits.literal_digits})", limit="literal_digits", configured=self.limits.literal_digits)
                return IntExpr(parse_decimal(raw), span)
            if kind == "BytesExpr":
                raw = obj["hex"]
                if not isinstance(raw, str): self.bad("hex must be a string")
                try: raw.encode("ascii")
                except UnicodeEncodeError: self.bad("hex must contain ASCII only")
                if len(raw) % 2 or any(x not in "0123456789abcdef" for x in raw): self.bad("byte hex must be lowercase and even length")
                return BytesExpr(bytes.fromhex(raw), span)
            if kind == "LocalExpr": return LocalExpr(s(obj["name"], "name"), span)
            if kind == "GlobalExpr": return GlobalExpr(s(obj["name"], "name"), span)
            if kind == "CallExpr": return CallExpr(n(obj["function"]), a(obj["arguments"], n), span, a(obj["type_arguments"], n))
            if kind == "BinaryExpr": return BinaryExpr(s(obj["operator"], "operator"), n(obj["left"]), n(obj["right"]), span)
            if kind == "LambdaExpr": return LambdaExpr(a(obj["parameters"], n), n(obj["result_type"]), n(obj["body"]), span)
            if kind == "IfExpr": return IfExpr(n(obj["condition"]), n(obj["then_body"]), n(obj["else_body"]), span)
            if kind == "ConstructorExpr": return ConstructorExpr(s(obj["constructor"], "constructor"), a(obj["arguments"], n), span, a(obj["type_arguments"], n))
            if kind == "PrimitiveExpr": return PrimitiveExpr(s(obj["name"], "name"), a(obj["arguments"], n), span)
            if kind == "LetBinding": return LetBinding(n(obj["binder"]), n(obj["value"]), span)
            if kind == "DeferCall": return DeferCall(n(obj["call"]), span)
            if kind == "Block": return Block(a(obj["bindings"], n), n(obj["result"]), span)
            if kind == "CaseAlternative": return CaseAlternative(s(obj["constructor"], "constructor"), a(obj["binders"], n), n(obj["body"]), span)
            if kind == "CaseExpr": return CaseExpr(n(obj["scrutinee"]), n(obj["result_type"]), a(obj["alternatives"], n), span)
            if kind == "ImportDecl": return ImportDecl(s(obj["module"], "module"), a(obj["names"], lambda x: s(x, "name")), span, self.boolean(obj["public"], "public") if "public" in obj else False)
            if kind == "TargetConstantPattern": return TargetConstantPattern(s(obj["name"], "name"), span)
            if kind == "TargetTuplePattern": return TargetTuplePattern(a(obj["items"], n), span)
            if kind == "Availability": return Availability(s(obj["selector"], "selector"), n(obj["pattern"]), span)
            if kind == "ConditionalImportAlternative": return ConditionalImportAlternative(n(obj["pattern"]), n(obj["import"]), span)
            if kind == "ConditionalImportDecl": return ConditionalImportDecl(s(obj["selector"], "selector"), a(obj["alternatives"], n), span)
            if kind == "FieldDecl": return FieldDecl(s(obj["name"], "name"), n(obj["type"]), span)
            if kind == "ConstructorDecl": return ConstructorDecl(s(obj["name"], "name"), a(obj["fields"], n), span)
            if kind == "TypeDecl": return TypeDecl(s(obj["name"], "name"), a(obj["constructors"], n), self.boolean(obj["public"], "public"), span, a(obj["type_parameters"], lambda x: s(x, "type parameter")), self.boolean(obj["owned"], "owned") if "owned" in obj else False)
            if kind == "IntrinsicTypeDecl": return IntrinsicTypeDecl(s(obj["name"], "name"), self.boolean(obj["public"], "public"), span)
            if kind == "FunctionDecl": return FunctionDecl(s(obj["name"], "name"), a(obj["parameters"], n), n(obj["result_type"]), n(obj["body"]), self.boolean(obj["public"], "public"), span, a(obj["type_parameters"], lambda x: s(x, "type parameter")))
            if kind == "IntrinsicFunctionDecl": return IntrinsicFunctionDecl(s(obj["name"], "name"), a(obj["parameters"], n), n(obj["result_type"]), s(obj["intrinsic"], "intrinsic"), self.boolean(obj["public"], "public"), span)
            if kind == "ForeignFunctionDecl": return ForeignFunctionDecl(s(obj["name"], "name"), a(obj["parameters"], n), n(obj["result_type"]), s(obj["binding"], "binding"), self.boolean(obj["public"], "public"), span)
            if kind == "ValueDecl": return ValueDecl(s(obj["name"], "name"), n(obj["type"]), n(obj["value"]), self.boolean(obj["public"], "public"), span)
            if kind == "Module":
                availability = None if "availability" not in obj or obj["availability"] is None else n(obj["availability"])
                return Module(s(obj["name"], "name"), a(obj["imports"], n), a(obj["types"], n), a(obj["functions"], n), a(obj["values"], n), span, availability)
            raise AssertionError(kind)
        finally: self.depth -= 1


def syntax_from_json(
    source: str | bytes, limits: Limits | None = None, *, version: int = 0
) -> Module:
    actual = limits or Limits()
    if isinstance(source, str):
        if len(source) > actual.input_bytes: fail("syntax.json.limit_exceeded", "json", f"input_bytes limit exceeded (configured {actual.input_bytes})", limit="input_bytes", configured=actual.input_bytes)
        try: data = source.encode("ascii")
        except UnicodeEncodeError: fail("syntax.json.non_ascii", "json", "JSON must contain ASCII only")
    elif isinstance(source, bytes): data = source
    else: raise TypeError("source must be str or bytes")
    if len(data) > actual.input_bytes: fail("syntax.json.limit_exceeded", "json", f"input_bytes limit exceeded (configured {actual.input_bytes})", limit="input_bytes", configured=actual.input_bytes)
    try: decoded = json.loads(data)
    except (json.JSONDecodeError, UnicodeDecodeError, ValueError) as error: fail("syntax.json.invalid", "json", "invalid JSON", error=str(error))
    root = _Decoder(actual).obj(decoded, {"schema", "version", "module"})
    if root["schema"] != "sloph.syntax" or type(root["version"]) is not int or root["version"] != version: fail("syntax.json.unsupported_schema", "json", f"expected sloph.syntax schema version {version}")
    module = _Decoder(actual).node(root["module"])
    if not isinstance(module, Module): fail("syntax.json.invalid", "json", "root node must be Module")
    from sloph.syntax.validate import validate_syntax
    validate_syntax(module, actual, version=version)
    return module


__all__ = ["syntax_from_json", "syntax_to_json"]

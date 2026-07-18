"""Authoritative fixed SlopH Core primitive signatures."""

from sloph.core.model import BYTES, INT, NamedType


FIXED_PRIMITIVES = {
    "int.add": ((INT, INT), INT),
    "int.sub": ((INT, INT), INT),
    "int.mul": ((INT, INT), INT),
    "int.equal": ((INT, INT), NamedType("sloph::Bool")),
    "int.less": ((INT, INT), NamedType("sloph::Bool")),
    "int.to_bytes": ((INT,), BYTES),
    "bytes.length": ((BYTES,), INT),
    "runtime.trap": ((BYTES,), NamedType("sloph::Unit")),
}

V0_PRIMITIVES = {name: FIXED_PRIMITIVES[name] for name in ("int.add", "int.sub", "int.mul")}

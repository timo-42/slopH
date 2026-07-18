from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any


@dataclass(frozen=True, slots=True)
class Span:
    start: int
    end: int


UNKNOWN_SPAN = Span(0, 0)


@dataclass(frozen=True, slots=True)
class Diagnostic:
    code: str
    phase: str
    message: str
    span: Span = UNKNOWN_SPAN
    details: dict[str, Any] = field(default_factory=dict)
    severity: str = "error"

    def as_dict(self) -> dict[str, Any]:
        return {
            "schema": "sloph.diagnostic",
            "version": 0,
            "code": self.code,
            "phase": self.phase,
            "severity": self.severity,
            "message_id": self.code,
            "message": self.message,
            "span": {"start": self.span.start, "end": self.span.end},
            "details": self.details,
        }

    def json_line(self) -> str:
        return json.dumps(
            self.as_dict(), ensure_ascii=True, sort_keys=True, separators=(",", ":")
        )

    def human(self, source_name: str = "<input>") -> str:
        return (
            f"{source_name}:{self.span.start}:{self.span.end}: "
            f"error[{self.code}]: {self.message}"
        )


class DiagnosticError(Exception):
    def __init__(self, diagnostic: Diagnostic):
        super().__init__(diagnostic.message)
        self.diagnostic = diagnostic


def fail(
    code: str,
    phase: str,
    message: str,
    span: Span = UNKNOWN_SPAN,
    **details: Any,
) -> None:
    raise DiagnosticError(Diagnostic(code, phase, message, span, details))


def limit_fail(
    phase: str, name: str, configured: int, span: Span = UNKNOWN_SPAN
) -> None:
    fail(
        f"core.{phase}.limit_exceeded",
        phase,
        f"{name} limit exceeded (configured {configured})",
        span,
        limit=name,
        configured=configured,
    )

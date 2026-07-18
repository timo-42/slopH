from __future__ import annotations

from dataclasses import dataclass
import platform

from sloph.core.diagnostics import fail


@dataclass(frozen=True, slots=True)
class CompilerSpecials:
    os: str
    arch: str

    def values(self, selector: str) -> tuple[str, ...]:
        if selector == "SPECIAL_PLATFORM":
            return (self.os, self.arch)
        if selector == "SPECIAL_ARCH":
            return (self.arch,)
        fail(
            "project.special.unknown",
            "resolve",
            f"unknown compiler special {selector!r}",
            special=selector,
        )

    @classmethod
    def host(cls) -> "CompilerSpecials":
        system = platform.system()
        machine = platform.machine().lower()
        os_name = {"Linux": "linux", "Darwin": "darwin"}.get(system)
        arch = {
            "x86_64": "amd64",
            "amd64": "amd64",
            "arm64": "arm64",
            "aarch64": "arm64",
        }.get(machine)
        if os_name is None or arch is None:
            fail(
                "compiler.target.unsupported_host",
                "environment",
                "the experimental compiler supports only Linux or Darwin on AMD64 or ARM64",
                system=system,
                machine=machine,
            )
        return cls(os_name, arch)


__all__ = ["CompilerSpecials"]

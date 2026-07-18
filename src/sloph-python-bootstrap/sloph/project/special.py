from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import platform

from sloph.core.diagnostics import fail


@dataclass(frozen=True, slots=True)
class CompilerTarget:
    os: "OS"
    arch: "Arch"

    def __post_init__(self) -> None:
        try:
            object.__setattr__(self, "os", OS(self.os))
            object.__setattr__(self, "arch", Arch(self.arch))
        except ValueError as error:
            fail(
                "compiler.target.invalid",
                "environment",
                "unknown compiler target value",
                os=str(self.os),
                arch=str(self.arch),
                error=str(error),
            )

    def value(self, selector: str) -> OS | Arch | tuple[OS, Arch]:
        if selector == "compiler::target::platform":
            return (self.os, self.arch)
        if selector == "compiler::target::arch":
            return self.arch
        fail(
            "project.target.unknown_selector",
            "resolve",
            f"unknown compiler target selector {selector!r}",
            selector=selector,
        )

    @classmethod
    def host(cls) -> "CompilerTarget":
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


class OS(str, Enum):
    LINUX = "linux"
    DARWIN = "darwin"


class Arch(str, Enum):
    AMD64 = "amd64"
    ARM64 = "arm64"


__all__ = ["Arch", "CompilerTarget", "OS"]

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

from sloph.core.model import ForeignBinding


@dataclass(frozen=True, slots=True)
class ProjectManifest:
    path: Path
    package: str
    source_root: Path
    entry: str
    dependencies: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class ProjectModule:
    name: str
    path: Path
    syntax: Any
    imports: tuple[str, ...]
    bundled: bool = False


@dataclass(frozen=True, slots=True)
class Project:
    manifest: ProjectManifest
    modules: tuple[ProjectModule, ...]
    foreign_bindings: tuple[ForeignBinding, ...] = ()

    def module(self, name: str) -> ProjectModule:
        for module in self.modules:
            if module.name == name:
                return module
        raise KeyError(name)

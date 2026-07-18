from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True, slots=True)
class ProjectManifest:
    path: Path
    package: str
    source_root: Path
    entry: str


@dataclass(frozen=True, slots=True)
class ProjectModule:
    name: str
    path: Path
    syntax: Any
    imports: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Project:
    manifest: ProjectManifest
    modules: tuple[ProjectModule, ...]

    def module(self, name: str) -> ProjectModule:
        for module in self.modules:
            if module.name == name:
                return module
        raise KeyError(name)

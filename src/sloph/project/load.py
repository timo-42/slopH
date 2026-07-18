from __future__ import annotations

import heapq
from pathlib import Path
import re
import tomllib
from typing import Any

from sloph.core.diagnostics import DiagnosticError, fail
from sloph.core.limits import Limits
from sloph.project.model import Project, ProjectManifest, ProjectModule


_LOWER_SEGMENT = re.compile(r"[a-z_][A-Za-z0-9_]*\Z", re.ASCII)
_GLOBAL = re.compile(
    r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)+\Z", re.ASCII
)
_MANIFEST_KEYS = frozenset(("format", "package", "source-root", "entry"))
_MANIFEST_BYTES = 65_536


def load_manifest(path: str | Path) -> ProjectManifest:
    supplied = Path(path)
    manifest_path = supplied / "sloph.toml" if supplied.is_dir() else supplied
    data = _read_bounded(manifest_path, _MANIFEST_BYTES, "project.manifest.limit")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        fail(
            "project.manifest.encoding",
            "project",
            "sloph.toml must be UTF-8",
            path=str(manifest_path),
            offset=error.start,
        )
    try:
        raw = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        fail(
            "project.manifest.syntax",
            "project",
            f"invalid sloph.toml: {error}",
            path=str(manifest_path),
        )
    if not isinstance(raw, dict):
        fail("project.manifest.shape", "project", "manifest must be a TOML table")
    unknown = sorted(set(raw) - _MANIFEST_KEYS)
    missing = sorted(_MANIFEST_KEYS - set(raw))
    if missing:
        fail(
            "project.manifest.missing",
            "project",
            "manifest is missing required fields",
            fields=missing,
        )
    if unknown:
        fail(
            "project.manifest.unknown",
            "project",
            "manifest contains unknown fields",
            fields=unknown,
        )
    if type(raw["format"]) is not int or raw["format"] != 0:
        fail(
            "project.manifest.format",
            "project",
            "only project manifest format 0 is supported",
            format=raw["format"],
        )
    package = _required_string(raw, "package")
    entry = _required_string(raw, "entry")
    source_root_text = _required_string(raw, "source-root")
    if not _LOWER_SEGMENT.fullmatch(package):
        fail(
            "project.manifest.package",
            "project",
            "package must be one lowercase-leading ASCII identifier segment",
            package=package,
        )
    if (
        not _GLOBAL.fullmatch(entry)
        or not entry.startswith(package + "::")
        or any(not _LOWER_SEGMENT.fullmatch(part) for part in entry.split("::"))
    ):
        fail(
            "project.manifest.entry",
            "project",
            "entry must be a fully qualified identity in this package",
            entry=entry,
        )
    relative_root = Path(source_root_text)
    if relative_root.is_absolute() or not relative_root.parts or any(
        part in ("", ".", "..") for part in relative_root.parts
    ):
        fail(
            "project.manifest.source_root",
            "project",
            "source-root must be a normalized relative path",
            source_root=source_root_text,
        )
    source_root = manifest_path.parent / relative_root
    if not source_root.is_dir():
        fail(
            "project.manifest.source_root",
            "project",
            "source-root is not a directory",
            source_root=str(source_root),
        )
    return ProjectManifest(manifest_path, package, source_root, entry)


def _required_string(raw: dict[str, object], key: str) -> str:
    value = raw[key]
    if not isinstance(value, str) or not value:
        fail(
            "project.manifest.field_type",
            "project",
            f"manifest field {key!r} must be a non-empty string",
            field=key,
        )
    return value


def load_project(
    path: str | Path, limits: Limits | None = None, *, source_version: int = 0
) -> Project:
    """Load, parse, and topologically order all modules in a project."""

    manifest = load_manifest(path)
    actual_limits = limits or Limits()
    from sloph.syntax import parse_source, parse_source_v1
    source_parser = parse_source_v1 if source_version == 1 else parse_source

    by_name: dict[str, ProjectModule] = {}
    sources: dict[str, bytes] = {}
    source_paths: list[Path] = []
    for source_path in manifest.source_root.rglob("*.sloph"):
        if not source_path.is_file():
            continue
        source_paths.append(source_path)
        if len(source_paths) > actual_limits.project_files:
            fail(
                "project.files.limit",
                "project",
                "project source-file limit exceeded",
                configured=actual_limits.project_files,
            )
    total_bytes = 0
    for source_path in sorted(source_paths):
        expected = _module_name(manifest, source_path)
        data = _read_bounded(
            source_path,
            actual_limits.input_bytes,
            "project.source.limit",
        )
        total_bytes += len(data)
        if total_bytes > actual_limits.project_bytes:
            fail(
                "project.bytes.limit",
                "project",
                "project source-byte limit exceeded",
                configured=actual_limits.project_bytes,
            )
        actual, imports = _scan_header(
            data, source_path, source_version=source_version
        )
        if actual != expected:
            fail(
                "project.module.path_mismatch",
                "resolve",
                f"module declaration {actual!r} does not match its path identity {expected!r}",
                path=str(source_path),
                expected=expected,
                actual=actual,
            )
        if actual in by_name:
            fail(
                "project.module.duplicate",
                "resolve",
                f"duplicate module {actual!r}",
                module=actual,
            )
        if len(imports) != len(set(imports)):
            fail(
                "project.import.duplicate",
                "resolve",
                f"module {actual!r} imports a module more than once",
                module=actual,
            )
        by_name[actual] = ProjectModule(actual, source_path, None, imports)
        sources[actual] = data
    if not by_name:
        fail(
            "project.module.none",
            "project",
            "source-root contains no .sloph modules",
            source_root=str(manifest.source_root),
        )
    for module in by_name.values():
        for imported in module.imports:
            if imported not in by_name:
                fail(
                    "project.import.missing",
                    "resolve",
                    f"module {module.name!r} imports missing module {imported!r}",
                    module=module.name,
                    imported=imported,
                )
    ordered_headers = _topological_modules(by_name)
    ordered: list[ProjectModule] = []
    for header in ordered_headers:
        try:
            syntax = source_parser(sources[header.name], actual_limits)
        except DiagnosticError as error:
            error.diagnostic.details.setdefault("path", str(header.path))
            raise
        parsed_name = _attribute(syntax, "name", "module")
        parsed_imports = tuple(
            _import_name(item) for item in _items(syntax, "imports")
        )
        if parsed_name != header.name or parsed_imports != header.imports:
            raise AssertionError("source header scan disagreed with the source parser")
        ordered.append(
            ProjectModule(header.name, header.path, syntax, header.imports)
        )
    return Project(manifest, tuple(ordered))


def _read_bounded(path: Path, maximum: int, code: str) -> bytes:
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    if len(data) > maximum:
        fail(
            code,
            "project",
            f"{path.name} exceeds the configured input limit",
            path=str(path),
            configured=maximum,
        )
    return data


class _HeaderCursor:
    def __init__(self, text: str, path: Path):
        self.text = text
        self.path = path
        self.index = 0

    def skip_space(self) -> None:
        while self.index < len(self.text) and self.text[self.index] in " \t\r\n":
            self.index += 1

    def word(self) -> str | None:
        self.skip_space()
        if self.index >= len(self.text):
            return None
        first = self.text[self.index]
        if not (first.isalpha() or first == "_"):
            return None
        start = self.index
        self.index += 1
        while self.index < len(self.text) and (
            self.text[self.index].isalnum() or self.text[self.index] == "_"
        ):
            self.index += 1
        return self.text[start : self.index]

    def peek_word(self) -> str | None:
        saved = self.index
        result = self.word()
        self.index = saved
        return result

    def take(self, token: str) -> bool:
        self.skip_space()
        if self.text.startswith(token, self.index):
            self.index += len(token)
            return True
        return False

    def require(self, token: str) -> None:
        if not self.take(token):
            _header_fail(self.path, f"expected {token!r} in source header")

    def require_word(self, description: str) -> str:
        result = self.word()
        if result is None:
            _header_fail(self.path, f"expected {description} in source header")
        return result


def _scan_header(
    data: bytes, path: Path, *, source_version: int = 0
) -> tuple[str, tuple[str, ...]]:
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as error:
        fail(
            "syntax.parse.non_ascii",
            "parse",
            "source must contain ASCII only",
            path=str(path),
            offset=error.start,
        )
    if source_version == 1:
        text = _comments_to_space(text, path)
    cursor = _HeaderCursor(text, path)
    if cursor.word() != "module":
        _header_fail(path, "source must begin with a module declaration")
    module = _scan_path(cursor, stop_at_selection=False)
    cursor.require(";")
    imports: list[str] = []
    while cursor.peek_word() == "import":
        cursor.word()
        imports.append(_scan_path(cursor, stop_at_selection=True))
        cursor.require("{")
        cursor.require_word("selected import name")
        while cursor.take(","):
            cursor.require_word("selected import name")
        cursor.require("}")
        cursor.require(";")
    return module, tuple(imports)


def _comments_to_space(text: str, path: Path) -> str:
    """Remove v1 comments without changing offsets or line structure."""
    chars = list(text)
    index = 0
    depth = 0
    while index < len(text):
        if depth == 0 and text.startswith("//", index):
            end = text.find("\n", index + 2)
            end = len(text) if end < 0 else end
            for position in range(index, end):
                chars[position] = " "
            index = end
            continue
        if text.startswith("/*", index):
            depth += 1
            chars[index] = chars[index + 1] = " "
            index += 2
            continue
        if depth and text.startswith("*/", index):
            depth -= 1
            chars[index] = chars[index + 1] = " "
            index += 2
            continue
        if depth and text[index] not in "\r\n":
            chars[index] = " "
        index += 1
    if depth:
        _header_fail(path, "unterminated block comment")
    return "".join(chars)


def _scan_path(cursor: _HeaderCursor, *, stop_at_selection: bool) -> str:
    parts = [cursor.require_word("identifier")]
    while cursor.take("::"):
        cursor.skip_space()
        if stop_at_selection and cursor.text.startswith("{", cursor.index):
            return "::".join(parts)
        parts.append(cursor.require_word("identifier"))
    if stop_at_selection:
        _header_fail(cursor.path, "import path must end in '::{'")
    return "::".join(parts)


def _header_fail(path: Path, message: str) -> None:
    fail(
        "project.header.syntax",
        "project",
        message,
        path=str(path),
    )


def _attribute(value: Any, *names: str) -> Any:
    for name in names:
        if hasattr(value, name):
            return getattr(value, name)
    raise TypeError(f"syntax object {type(value).__name__} lacks {names!r}")


def _items(value: Any, name: str) -> tuple[Any, ...]:
    result = getattr(value, name, ())
    return tuple(result)


def _import_name(item: Any) -> str:
    return item if isinstance(item, str) else _attribute(item, "module", "name")


def _module_name(manifest: ProjectManifest, path: Path) -> str:
    relative = path.relative_to(manifest.source_root)
    segments = relative.with_suffix("").parts
    if not segments or any(not _LOWER_SEGMENT.fullmatch(segment) for segment in segments):
        fail(
            "project.module.path",
            "project",
            "module paths must contain lowercase-leading ASCII identifier segments",
            path=str(relative),
        )
    return "::".join((manifest.package, *segments))


def _topological_modules(
    modules: dict[str, ProjectModule],
) -> tuple[ProjectModule, ...]:
    dependents: dict[str, list[str]] = {name: [] for name in modules}
    indegree: dict[str, int] = {}
    for name, module in modules.items():
        indegree[name] = len(module.imports)
        for imported in module.imports:
            dependents[imported].append(name)
    ready = [name for name, degree in indegree.items() if degree == 0]
    heapq.heapify(ready)
    result: list[ProjectModule] = []
    while ready:
        current = heapq.heappop(ready)
        result.append(modules[current])
        for dependent in sorted(dependents[current]):
            indegree[dependent] -= 1
            if indegree[dependent] == 0:
                heapq.heappush(ready, dependent)
    if len(result) != len(modules):
        remaining = {name for name, degree in indegree.items() if degree}
        cycle = _deterministic_cycle(modules, remaining)
        fail(
            "project.import.cycle",
            "resolve",
            "module imports must be acyclic",
            modules=cycle,
        )
    return tuple(result)


def _deterministic_cycle(
    modules: dict[str, ProjectModule], candidates: set[str]
) -> list[str]:
    state: dict[str, int] = {}
    parent: dict[str, str] = {}
    for start in sorted(candidates):
        if state.get(start, 0):
            continue
        state[start] = 1
        stack: list[tuple[str, Any]] = [
            (start, iter(sorted(set(modules[start].imports) & candidates)))
        ]
        while stack:
            current, edges = stack[-1]
            try:
                target = next(edges)
            except StopIteration:
                state[current] = 2
                stack.pop()
                continue
            if state.get(target, 0) == 0:
                parent[target] = current
                state[target] = 1
                stack.append(
                    (target, iter(sorted(set(modules[target].imports) & candidates)))
                )
            elif state[target] == 1:
                cycle = [target]
                cursor = current
                while cursor != target:
                    cycle.append(cursor)
                    cursor = parent[cursor]
                return sorted(cycle)
    raise AssertionError("Kahn remainder did not contain an import cycle")

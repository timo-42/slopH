from __future__ import annotations

import heapq
import json
from dataclasses import replace
from pathlib import Path
import re
import tomllib
from typing import Any

from sloph._resources import libraries_root
from sloph.core.diagnostics import DiagnosticError, fail
from sloph.core.limits import Limits
from sloph.core.model import INT, ForeignBinding, NamedType
from sloph.project.model import Project, ProjectManifest, ProjectModule
from sloph.project.special import Arch, CompilerTarget, OS
from sloph.syntax.model import ConditionalImportDecl, ImportDecl, TargetConstantPattern, TargetPattern, TargetTuplePattern


_LOWER_SEGMENT = re.compile(r"[a-z_][A-Za-z0-9_]*\Z", re.ASCII)
_GLOBAL = re.compile(
    r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)+\Z", re.ASCII
)
_MANIFEST_KEYS = frozenset(("format", "package", "source-root", "entry", "dependencies"))
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
    missing = sorted({"format", "package", "source-root", "entry"} - set(raw))
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
    dependencies_raw = raw.get("dependencies", [])
    if not isinstance(dependencies_raw, list) or not all(
        isinstance(item, str) and _LOWER_SEGMENT.fullmatch(item)
        for item in dependencies_raw
    ):
        fail(
            "project.manifest.dependencies",
            "project",
            "dependencies must be an array of lowercase package names",
            path=str(manifest_path),
        )
    dependencies = tuple(dependencies_raw)
    if len(dependencies) != len(set(dependencies)):
        fail(
            "project.manifest.dependencies",
            "project",
            "dependencies must not contain duplicates",
            path=str(manifest_path),
        )
    return ProjectManifest(manifest_path, package, source_root, entry, dependencies)


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
    path: str | Path,
    limits: Limits | None = None,
    *,
    source_version: int = 0,
    target: CompilerTarget | None = None,
) -> Project:
    """Load, parse, and topologically order all modules in a project."""

    manifest = load_manifest(path)
    actual_limits = limits or Limits()
    actual_target = target or CompilerTarget.host()
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
        syntax = source_parser(data, actual_limits) if source_version == 1 else None
        if syntax is None:
            actual, imports = _scan_header(data, source_path, source_version=0)
        else:
            actual = syntax.name
            selected = _select_imports(syntax.imports, actual_target, source_path)
            imports = tuple(item.module for item in selected)
            syntax = replace(syntax, imports=selected)
            _require_available(syntax, actual_target, source_path)
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
        by_name[actual] = ProjectModule(actual, source_path, syntax, imports)
        sources[actual] = data
    if source_version == 1:
        _load_bundled_dependencies(
            ("core", *manifest.dependencies),
            by_name,
            sources,
            actual_limits,
            actual_target,
        )
    if not by_name:
        fail(
            "project.module.none",
            "project",
            "source-root contains no .sloph modules",
            source_root=str(manifest.source_root),
        )
    reachable = _reachable_modules(by_name)
    selected_by_name = {name: by_name[name] for name in reachable}
    for module in selected_by_name.values():
        if module.syntax is not None:
            _require_available(module.syntax, actual_target, module.path)
        for imported in module.imports:
            if imported not in by_name:
                fail(
                    "project.import.missing",
                    "resolve",
                    f"module {module.name!r} imports missing module {imported!r}",
                    module=module.name,
                    imported=imported,
                )
    ordered_headers = _topological_modules(selected_by_name)
    ordered: list[ProjectModule] = []
    for header in ordered_headers:
        syntax = header.syntax
        if syntax is None:
            try:
                syntax = source_parser(sources[header.name], actual_limits)
            except DiagnosticError as error:
                error.diagnostic.details.setdefault("path", str(header.path))
                raise
        parsed_name = _attribute(syntax, "name", "module")
        parsed_imports = tuple(_import_name(item) for item in _items(syntax, "imports"))
        if parsed_name != header.name or parsed_imports != header.imports:
            raise AssertionError("source header scan disagreed with the source parser")
        ordered.append(
            ProjectModule(
                header.name,
                header.path,
                syntax,
                header.imports,
                header.bundled,
            )
        )
    foreign_bindings: list[ForeignBinding] = []
    for module in ordered:
        if module.bundled:
            foreign_bindings.extend(_load_module_provider(module))
    return Project(manifest, tuple(ordered), tuple(foreign_bindings))


def _load_bundled_dependencies(
    requested: tuple[str, ...],
    by_name: dict[str, ProjectModule],
    sources: dict[str, bytes],
    limits: Limits,
    target: CompilerTarget,
) -> None:
    for package, package_root in resolve_bundled_packages(requested):
        source_root = package_root / "src"
        for source_path in sorted(source_root.rglob("*.sloph")):
            relative = source_path.relative_to(source_root).with_suffix("")
            expected = _library_module_name(package, relative)
            data = _read_bounded(source_path, limits.input_bytes, "project.source.limit")
            from sloph.syntax import parse_source_v1
            syntax = parse_source_v1(data, limits)
            actual = syntax.name
            selected = _select_imports(syntax.imports, target, source_path)
            imports = tuple(item.module for item in selected)
            syntax = replace(syntax, imports=selected)
            if actual != expected:
                fail("project.module.path_mismatch", "resolve", f"module declaration {actual!r} does not match its path identity {expected!r}", path=str(source_path), expected=expected, actual=actual)
            if actual in by_name:
                fail("project.module.duplicate", "resolve", f"duplicate module {actual!r}", module=actual)
            by_name[actual] = ProjectModule(actual, source_path, syntax, imports, True)
            sources[actual] = data


def resolve_bundled_packages(
    requested: tuple[str, ...],
) -> tuple[tuple[str, Path], ...]:
    """Resolve bundled package roots in dependency-first build order."""

    root = libraries_root()
    ordered: list[tuple[str, Path]] = []
    complete: set[str] = set()
    active: list[str] = []

    def visit(package: str) -> None:
        if package in complete:
            return
        if package in active:
            cycle = active[active.index(package):] + [package]
            fail(
                "project.dependency.cycle",
                "project",
                "bundled dependency graph contains a cycle",
                cycle=cycle,
            )
        package_root = root / package
        manifest_path = package_root / "library.json"
        if not manifest_path.is_file():
            fail(
                "project.dependency.missing",
                "project",
                f"bundled dependency {package!r} does not exist",
                dependency=package,
            )
        try:
            metadata = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            fail(
                "project.dependency.manifest",
                "project",
                f"invalid bundled dependency manifest for {package!r}: {error}",
                dependency=package,
            )
        if not isinstance(metadata, dict) or set(metadata) != {"format", "package", "dependencies"}:
            fail("project.dependency.manifest", "project", f"invalid bundled dependency manifest for {package!r}", dependency=package)
        if metadata["format"] != 0 or metadata["package"] != package:
            fail("project.dependency.manifest", "project", f"bundled dependency identity mismatch for {package!r}", dependency=package)
        dependencies = metadata["dependencies"]
        if not isinstance(dependencies, list) or not all(isinstance(item, str) and _LOWER_SEGMENT.fullmatch(item) for item in dependencies):
            fail("project.dependency.manifest", "project", f"invalid dependency list for {package!r}", dependency=package)
        if len(dependencies) != len(set(dependencies)):
            fail("project.dependency.manifest", "project", f"duplicate dependency in manifest for {package!r}", dependency=package)
        active.append(package)
        for dependency in dependencies:
            visit(dependency)
        active.pop()
        complete.add(package)
        ordered.append((package, package_root))

    for package in requested:
        visit(package)
    return tuple(ordered)


def _library_module_name(package: str, relative: Path) -> str:
    """Map root.sloph to the package module and other files below it."""

    if relative.parts == ("root",):
        return package
    return "::".join((package, *relative.parts))


def _select_imports(
    imports: tuple,
    target: CompilerTarget,
    path: Path,
) -> tuple[ImportDecl, ...]:
    selected: list[ImportDecl] = []
    for item in imports:
        if isinstance(item, ImportDecl):
            selected.append(item)
            continue
        if not isinstance(item, ConditionalImportDecl):
            raise TypeError(f"unsupported import node {type(item).__name__}")
        actual = target.value(item.selector)
        matches = [alt.import_ for alt in item.alternatives if _target_pattern_value(alt.pattern) == actual]
        if len(matches) != 1:
            fail(
                "project.target.no_match",
                "resolve",
                f"conditional import has no branch for {item.selector}={actual!r}",
                item.span,
                path=str(path),
                selector=item.selector,
                actual=_target_display(actual),
                available=[_target_pattern_display(alt.pattern) for alt in item.alternatives],
            )
        selected.append(matches[0])
    return tuple(selected)


def _require_available(syntax: Any, target: CompilerTarget, path: Path) -> None:
    availability = getattr(syntax, "availability", None)
    if availability is None:
        return
    actual = target.value(availability.selector)
    required = _target_pattern_value(availability.pattern)
    if required != actual:
        fail(
            "project.module.unavailable",
            "resolve",
            f"module {syntax.name!r} is unavailable for {actual!r}",
            availability.span,
            path=str(path),
            module=syntax.name,
            selector=availability.selector,
            required=_target_display(required),
            actual=_target_display(actual),
        )


_TARGET_CONSTANTS = {
    "os::linux": OS.LINUX,
    "os::darwin": OS.DARWIN,
    "arch::amd64": Arch.AMD64,
    "arch::arm64": Arch.ARM64,
}


def _target_pattern_value(pattern: TargetPattern):
    if isinstance(pattern, TargetConstantPattern):
        return _TARGET_CONSTANTS[pattern.name]
    if isinstance(pattern, TargetTuplePattern):
        return tuple(_target_pattern_value(item) for item in pattern.items)
    raise TypeError(f"unsupported compiler-target pattern {type(pattern).__name__}")


def _target_display(value):
    if isinstance(value, tuple):
        return [_target_display(item) for item in value]
    return value.value


def _target_pattern_display(pattern: TargetPattern):
    if isinstance(pattern, TargetConstantPattern):
        return pattern.name
    return [_target_pattern_display(item) for item in pattern.items]


def _reachable_modules(modules: dict[str, ProjectModule]) -> set[str]:
    roots = {name for name, module in modules.items() if not module.bundled}
    if "core" in modules:
        roots.add("core")
    pending = sorted(roots)
    reached: set[str] = set()
    while pending:
        name = pending.pop()
        if name in reached:
            continue
        module = modules.get(name)
        if module is None:
            # The caller emits the established missing-import diagnostic.
            continue
        reached.add(name)
        pending.extend(reversed(module.imports))
    return reached


def _load_module_provider(module: ProjectModule) -> tuple[ForeignBinding, ...]:
    provider_root = module.path.with_suffix("")
    manifest_path = provider_root / "provider.json"
    if not manifest_path.is_file():
        return ()
    try:
        metadata = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail("project.provider.syntax", "project", f"invalid native provider metadata: {error}", path=str(manifest_path))
    if not isinstance(metadata, dict) or set(metadata) != {"format", "module", "bindings", "libraries"}:
        fail("project.provider.shape", "project", "native provider metadata has missing or unknown fields", path=str(manifest_path))
    if metadata["format"] != 0 or metadata["module"] != module.name:
        fail("project.provider.identity", "project", "native provider identity does not match its module", path=str(manifest_path), module=module.name)
    binding_name = metadata["bindings"]
    libraries = metadata["libraries"]
    if not isinstance(binding_name, str) or Path(binding_name).name != binding_name:
        fail("project.provider.shape", "project", "provider bindings must be a local filename", path=str(manifest_path))
    if not isinstance(libraries, list) or not libraries or not all(isinstance(item, str) and Path(item).name == item and item.endswith((".so", ".dylib")) for item in libraries):
        fail("project.provider.shape", "project", "provider libraries must be local shared-library filenames", path=str(manifest_path))
    if len(libraries) != len(set(libraries)):
        fail("project.provider.shape", "project", "provider libraries must not contain duplicates", path=str(manifest_path))
    binding_path = provider_root / binding_name
    try:
        bindings = json.loads(binding_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail("project.foreign_binding.syntax", "project", f"invalid foreign binding metadata: {error}", path=str(binding_path))
    if not isinstance(bindings, list):
        fail("project.foreign_binding.shape", "project", "foreign binding metadata must be an array", path=str(binding_path))
    decoded = tuple(_decode_foreign_binding(item, binding_path) for item in bindings)
    if any(binding.provider != module.name for binding in decoded):
        fail("project.provider.binding", "project", "foreign binding names a different provider", path=str(binding_path), provider=module.name)
    for binding in decoded:
        if Path(binding.header).name != binding.header or not (provider_root / binding.header).is_file():
            fail("project.provider.header", "project", "native provider header is missing or invalid", path=str(provider_root / binding.header), provider=module.name)
    return decoded


def _decode_foreign_binding(raw: object, path: Path) -> ForeignBinding:
    if not isinstance(raw, dict):
        fail("project.foreign_binding.shape", "project", "foreign binding must be an object", path=str(path))
    required = {"identity", "symbol", "c_parameters", "c_result", "provider", "requires", "effects", "facts", "provenance", "header"}
    allowed = required | {"adapter"}
    if not required <= set(raw) or set(raw) - allowed:
        fail("project.foreign_binding.shape", "project", "foreign binding has missing or unknown fields", path=str(path))
    adapter = raw.get("adapter")
    if adapter is None:
        # A raw pointer binding is visible to audit tooling, but not callable
        # until Source has a writable borrowed-buffer type.
        parameters: tuple = ()
        result = NamedType("core::Unit")
        adapter_kind = "unavailable"
    else:
        expected = {"kind", "arguments", "result", "sloph_parameters", "sloph_result"}
        if not isinstance(adapter, dict) or set(adapter) != expected:
            fail("project.foreign_binding.adapter", "project", "invalid foreign adapter metadata", path=str(path))
        source_parameters = adapter["sloph_parameters"]
        if not isinstance(source_parameters, list):
            fail("project.foreign_binding.adapter", "project", "adapter parameters must be an array", path=str(path))
        parameters = tuple(_binding_type(item, path) for item in source_parameters)
        result = _binding_type(adapter["sloph_result"], path)
        adapter_kind = _binding_string(adapter["kind"], path)
    c_parameters = raw["c_parameters"]
    facts = raw["facts"]
    if not isinstance(c_parameters, list) or not all(isinstance(item, str) for item in c_parameters):
        fail("project.foreign_binding.shape", "project", "C parameters must be strings", path=str(path))
    if not isinstance(facts, dict) or not all(isinstance(key, str) and isinstance(value, str) for key, value in facts.items()):
        fail("project.foreign_binding.shape", "project", "binding facts must be string pairs", path=str(path))

    def strings(field: str) -> tuple[str, ...]:
        value = raw[field]
        if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
            fail("project.foreign_binding.shape", "project", f"binding {field} must be an array of strings", path=str(path))
        return tuple(value)

    return ForeignBinding(
        _binding_string(raw["identity"], path),
        _binding_string(raw["symbol"], path),
        parameters,
        result,
        adapter_kind,
        tuple(c_parameters),
        _binding_string(raw["c_result"], path),
        _binding_string(raw["provider"], path),
        _binding_string(raw["header"], path),
        strings("requires"),
        strings("effects"),
        tuple(sorted(facts.items())),
        _binding_string(raw["provenance"], path),
    )


def _binding_string(value: object, path: Path) -> str:
    if not isinstance(value, str) or not value:
        fail("project.foreign_binding.shape", "project", "binding value must be a non-empty string", path=str(path))
    return value


def _binding_type(value: object, path: Path):
    name = _binding_string(value, path)
    if name == "Int":
        return INT
    if name in {"Bytes", "Unit", "Bool"}:
        return NamedType(f"core::{name}")
    return NamedType(name)


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

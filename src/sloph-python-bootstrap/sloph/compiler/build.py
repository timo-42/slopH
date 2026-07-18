from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import platform
import signal
import shutil
import subprocess
import tempfile
import threading
from time import perf_counter_ns

from sloph._resources import libraries_root
from sloph.backend import emit_c
from sloph.core.diagnostics import fail
from sloph.core.model import CoreUnit
from sloph.project import elaborate_project, load_manifest, load_project, resolve_bundled_packages


@dataclass(frozen=True, slots=True)
class BuildResult:
    output: Path
    symbol: str
    timings_ns: dict[str, int]
    c_bytes: int


def compile_project(
    project: str | Path,
    output: str | Path,
    *,
    cc: str = "cc",
    emit_c_path: str | Path | None = None,
    source_version: int = 0,
) -> BuildResult:
    start = perf_counter_ns()
    if source_version == 1:
        manifest = load_manifest(project)
        packages = resolve_bundled_packages(("prelude", *manifest.dependencies))
        _run_package_build_scripts(packages)
    dependencies_built = perf_counter_ns()
    loaded = load_project(project, source_version=source_version)
    if source_version == 1:
        from sloph.project import elaborate_project_v1
        unit = elaborate_project_v1(loaded)
    else:
        unit = elaborate_project(loaded)
    lowered = perf_counter_ns()
    return _compile(
        unit,
        loaded.manifest.entry,
        output,
        cc=cc,
        emit_c_path=emit_c_path,
        prior_timings={
            "dependency_build": dependencies_built - start,
            "source_to_core": lowered - dependencies_built,
        },
    )


def compile_core(
    unit: CoreUnit,
    symbol: str,
    output: str | Path,
    *,
    cc: str = "cc",
    emit_c_path: str | Path | None = None,
) -> BuildResult:
    return _compile(unit, symbol, output, cc=cc, emit_c_path=emit_c_path)


def _compile(
    unit: CoreUnit,
    symbol: str,
    output: str | Path,
    *,
    cc: str,
    emit_c_path: str | Path | None,
    prior_timings: dict[str, int] | None = None,
) -> BuildResult:
    timings = dict(prior_timings or {})
    start = perf_counter_ns()
    c_source = emit_c(unit, symbol)
    emitted = perf_counter_ns()
    timings["core_to_c"] = emitted - start
    if emit_c_path is not None:
        _atomic_text(Path(emit_c_path), c_source)

    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    compiler = _resolve_compiler(cc)
    flags = _target_flags()
    native_inputs = _native_inputs(unit)
    with tempfile.TemporaryDirectory(prefix="sloph-c11-") as directory:
        root = Path(directory)
        source_path = root / "program.c"
        binary_path = root / "program"
        source_path.write_text(c_source, encoding="ascii", newline="\n")
        include_arguments = [
            argument
            for include_root, _ in native_inputs
            for argument in ("-I", str(include_root))
        ]
        provider_libraries = [
            str(library)
            for _, libraries in native_inputs
            for library in libraries
        ]
        runtime_arguments = [
            f"-Wl,-rpath,{include_root}"
            for include_root, _ in native_inputs
        ]
        command = [
            compiler, *flags, *include_arguments, str(source_path),
            *provider_libraries, *runtime_arguments, "-o", str(binary_path),
        ]
        returncode, stderr_bytes = _run_process_bounded(command, compiler)
        if returncode != 0:
            stderr = stderr_bytes.decode("utf-8", errors="replace")
            fail(
                "compiler.c11.failed",
                "environment",
                "C compiler failed",
                compiler=compiler,
                exit_code=returncode,
                stderr=stderr,
            )
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{destination.name}.tmp-", dir=destination.parent
        )
        os.close(descriptor)
        temporary = Path(temporary_name)
        try:
            shutil.copyfile(binary_path, temporary)
            temporary.chmod(0o755)
            temporary.replace(destination)
        finally:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
    timings["c_compile_link"] = perf_counter_ns() - emitted
    return BuildResult(destination, symbol, timings, len(c_source.encode("ascii")))


def run_project(
    project: str | Path,
    *,
    cc: str = "cc",
    emit_c_path: str | Path | None = None,
    source_version: int = 0,
) -> tuple[BuildResult, subprocess.CompletedProcess[bytes]]:
    with tempfile.TemporaryDirectory(prefix="sloph-run-") as directory:
        executable = Path(directory) / "program"
        result = compile_project(
            project,
            executable,
            cc=cc,
            emit_c_path=emit_c_path,
            source_version=source_version,
        )
        try:
            completed = subprocess.run(
                [str(executable)], check=False, capture_output=True, timeout=30
            )
        except subprocess.TimeoutExpired:
            fail(
                "compiler.run.timeout",
                "environment",
                "compiled program exceeded the 30 second limit",
            )
        stable_result = BuildResult(
            Path("<temporary>"), result.symbol, result.timings_ns, result.c_bytes
        )
        return stable_result, completed


def _resolve_compiler(value: str) -> str:
    if not value:
        fail("compiler.c11.path", "environment", "C compiler path is empty")
    if os.sep in value or (os.altsep is not None and os.altsep in value):
        path = Path(value)
        if not path.is_file() or not os.access(path, os.X_OK):
            fail(
                "compiler.c11.path",
                "environment",
                f"C compiler {value!r} is not an executable file",
                compiler=value,
            )
        return str(path)
    found = shutil.which(value)
    if found is None:
        fail(
            "compiler.c11.path",
            "environment",
            f"C compiler {value!r} was not found on PATH",
            compiler=value,
        )
    return found


def _run_process_bounded(
    command: list[str],
    compiler: str,
    *,
    cwd: Path | None = None,
    diagnostic_prefix: str = "compiler.c11",
    description: str = "C compiler",
    details: dict[str, object] | None = None,
) -> tuple[int, bytes]:
    """Run a build process with bounded time and diagnostic output."""
    diagnostic_details = {"compiler": compiler} if details is None else details
    try:
        process = subprocess.Popen(
            command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            start_new_session=True,
        )
    except OSError as error:
        fail(
            f"{diagnostic_prefix}.launch",
            "environment",
            f"{description} could not be started",
            **diagnostic_details,
            error=str(error),
        )
    limit = 1_048_576
    tail_limit = 65_536
    total = 0
    exceeded = False
    lock = threading.Lock()
    tails = {"stdout": bytearray(), "stderr": bytearray()}

    def kill_group() -> None:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    def drain(name: str, stream: object) -> None:
        nonlocal total, exceeded
        while True:
            chunk = stream.read(8192)  # type: ignore[attr-defined]
            if not chunk:
                return
            should_kill = False
            with lock:
                total += len(chunk)
                tail = tails[name]
                tail.extend(chunk)
                if len(tail) > tail_limit:
                    del tail[:-tail_limit]
                if total > limit and not exceeded:
                    exceeded = True
                    should_kill = True
            if should_kill:
                kill_group()

    threads = [
        threading.Thread(target=drain, args=("stdout", process.stdout), daemon=True),
        threading.Thread(target=drain, args=("stderr", process.stderr), daemon=True),
    ]
    for thread in threads:
        thread.start()
    try:
        returncode = process.wait(timeout=120)
    except subprocess.TimeoutExpired:
        kill_group()
        process.wait()
        for thread in threads:
            thread.join(timeout=2)
        process.stdout.close()
        process.stderr.close()
        fail(
            f"{diagnostic_prefix}.timeout",
            "environment",
            f"{description} exceeded the 120 second limit",
            **diagnostic_details,
        )
    for thread in threads:
        thread.join(timeout=0.1)
    if any(thread.is_alive() for thread in threads):
        kill_group()
        for thread in threads:
            thread.join(timeout=2)
    if any(thread.is_alive() for thread in threads):
        process.stdout.close()
        process.stderr.close()
        fail(
            f"{diagnostic_prefix}.pipe_timeout",
            "environment",
            f"{description} diagnostic pipes did not close",
            **diagnostic_details,
        )
    process.stdout.close()
    process.stderr.close()
    if exceeded:
        fail(
            f"{diagnostic_prefix}.output_limit",
            "environment",
            f"{description} output exceeded 1048576 bytes",
            **diagnostic_details,
            configured=limit,
        )
    return returncode, bytes(tails["stderr"])


def _target_flags() -> list[str]:
    system = platform.system()
    machine = platform.machine().lower()
    common = ["-std=c11", "-O0", "-g0", "-Wall", "-Wextra", "-Werror"]
    if system == "Darwin" and machine == "arm64":
        # Current dyld requires LC_UUID even for unsigned local executables.
        return common
    if system == "Linux" and machine in ("x86_64", "amd64"):
        return [*common, "-Wl,--build-id=none"]
    fail(
        "compiler.c11.unsupported_host",
        "environment",
        "the experimental native bridge supports only macOS ARM64 and Linux AMD64",
        system=system,
        machine=machine,
    )


def _native_inputs(unit: CoreUnit) -> tuple[tuple[Path, tuple[Path, ...]], ...]:
    result = []
    for identity in sorted({binding.provider for binding in unit.foreign_bindings}):
        parts = identity.split("::")
        if len(parts) < 2 or any(not part or not part.replace("_", "a").isalnum() for part in parts):
            fail("compiler.provider.identity", "backend", "invalid native provider identity", provider=identity)
        root = libraries_root() / parts[0] / "src" / Path(*parts[1:])
        manifest_path = root / "provider.json"
        try:
            metadata = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            fail("compiler.provider.metadata", "environment", f"native provider metadata could not be loaded: {error}", provider=identity, path=str(manifest_path))
        if not isinstance(metadata, dict) or set(metadata) != {"format", "module", "bindings", "libraries"} or metadata.get("format") != 0 or metadata.get("module") != identity:
            fail("compiler.provider.metadata", "environment", "native provider metadata is invalid", provider=identity, path=str(manifest_path))
        names = metadata["libraries"]
        if not isinstance(names, list) or not names or not all(isinstance(name, str) and Path(name).name == name and name.endswith((".so", ".dylib")) for name in names) or len(names) != len(set(names)):
            fail("compiler.provider.metadata", "environment", "native provider libraries are invalid", provider=identity, path=str(manifest_path))
        libraries = tuple(root / name for name in names)
        missing = [str(library) for library in libraries if not library.is_file()]
        if missing:
            fail("compiler.provider.library", "environment", "native provider library has not been built", provider=identity, missing=missing)
        result.append((root, libraries))
    return tuple(result)


def _run_package_build_scripts(packages: tuple[tuple[str, Path], ...]) -> None:
    for package, root in packages:
        script = root / "build.sh"
        if not script.exists():
            continue
        if script.is_symlink() or not script.is_file() or not os.access(script, os.X_OK):
            fail(
                "compiler.build_script.invalid",
                "environment",
                "package build.sh must be an executable regular file",
                package=package,
                script=str(script),
            )
        returncode, stderr_bytes = _run_process_bounded(
            [str(script)],
            str(script),
            cwd=root,
            diagnostic_prefix="compiler.build_script",
            description="package build script",
            details={"package": package, "script": str(script)},
        )
        if returncode != 0:
            fail(
                "compiler.build_script.failed",
                "environment",
                "package build.sh failed",
                package=package,
                script=str(script),
                exit_code=returncode,
                stderr=stderr_bytes.decode("utf-8", errors="replace"),
            )


def _atomic_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.tmp-", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="ascii", newline="\n") as stream:
            stream.write(content)
        temporary.replace(path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass

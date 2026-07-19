from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import subprocess
import sys
import tempfile


REQUIRED = {"format", "name", "kind", "input", "expect-exit"}
OPTIONAL = {"symbol", "fuel", "expect-output", "expect-diagnostics"}
KINDS = {
    "core-check", "core-print", "core-eval", "core-run", "source-check", "source-format",
    "source-ast", "source-core", "source-run", "v1-check", "v1-format", "v1-ast",
    "v1-core", "v1-run", "v1-native",
}


@dataclass(frozen=True)
class Case:
    directory: Path
    fields: dict[str, str]

    def path(self, field: str) -> Path:
        candidate = (self.directory / self.fields[field]).resolve()
        root = self.directory.resolve()
        if candidate != root and root not in candidate.parents:
            raise ValueError(f"{field} escapes the case directory")
        return candidate


@dataclass(frozen=True)
class Outcome:
    exit_code: int
    stdout: str
    stderr: str


def load_case(path: Path) -> Case:
    fields: dict[str, str] = {}
    for number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            raise ValueError(f"{path}:{number}: expected key: value")
        key, value = (part.strip() for part in line.split(":", 1))
        if not key or not value:
            raise ValueError(f"{path}:{number}: empty key or value")
        if key in fields:
            raise ValueError(f"{path}:{number}: duplicate field {key}")
        if key not in REQUIRED | OPTIONAL:
            raise ValueError(f"{path}:{number}: unknown field {key}")
        fields[key] = value
    missing = REQUIRED - fields.keys()
    if missing:
        raise ValueError(f"{path}: missing fields {sorted(missing)}")
    if fields["format"] != "0":
        raise ValueError(f"{path}: unsupported case format")
    if fields["kind"] not in KINDS:
        raise ValueError(f"{path}: unknown kind {fields['kind']}")
    try:
        expected_exit = int(fields["expect-exit"])
    except ValueError as error:
        raise ValueError(f"{path}: expect-exit must be an integer") from error
    allowed_exit = range(256) if fields["kind"] == "v1-native" else range(5)
    if expected_exit not in allowed_exit:
        raise ValueError(f"{path}: invalid expect-exit")
    if fields["kind"] not in {"core-eval", "core-run"} and ({"symbol", "fuel"} & fields.keys()):
        raise ValueError(f"{path}: symbol and fuel are only valid for Core execution")
    if fields["kind"] in {"core-eval", "core-run"} and "symbol" not in fields:
        raise ValueError(f"{path}: {fields['kind']} requires symbol")
    if fields["kind"] == "core-run" and "fuel" in fields:
        raise ValueError(f"{path}: fuel is not valid for core-run")
    if "fuel" in fields:
        try:
            if int(fields["fuel"]) <= 0:
                raise ValueError
        except ValueError as error:
            raise ValueError(f"{path}: fuel must be a positive integer") from error
    case = Case(path.parent.resolve(), fields)
    if not case.path("input").exists():
        raise ValueError(f"{path}: missing input path")
    for field in ("expect-output", "expect-diagnostics"):
        if field in fields and not case.path(field).is_file():
            raise ValueError(f"{path}: missing {field} file")
    return case


def discover(root: Path) -> list[Case]:
    cases = [load_case(path) for path in sorted(root.glob("**/case.test"))]
    names: set[str] = set()
    for case in cases:
        name = case.fields["name"]
        if name in names:
            raise ValueError(f"duplicate case name {name!r}")
        names.add(name)
    return cases


def run(case: Case) -> Outcome:
    kind = case.fields["kind"]
    stable = [sys.executable, "-m", "sloph", "--diagnostics", "jsonl"]
    prefix = [*stable, "unstable"]
    input_path = str(case.path("input"))
    if kind in {"core-check", "core-print", "core-eval"}:
        action = {"core-check": "check", "core-print": "print", "core-eval": "eval"}[kind]
        command = [*prefix, "core", action, input_path]
    elif kind == "core-run":
        command = []
    elif kind == "source-check":
        command = [*prefix, "check", input_path]
    elif kind == "source-format":
        command = [*prefix, "format", input_path, "--stdout"]
    elif kind == "source-ast":
        command = [*prefix, "ast", "print", input_path, "--format", "json"]
    elif kind == "source-core":
        command = [*prefix, "core", "print", input_path, "--input-format", "source"]
    elif kind == "source-run":
        command = [*prefix, "run", input_path]
    elif kind == "v1-check":
        command = [*stable, "check", input_path]
    elif kind == "v1-format":
        command = [*stable, "format", input_path, "--stdout"]
    elif kind == "v1-ast":
        command = [*stable, "ast", "print", input_path, "--format", "json"]
    elif kind == "v1-core":
        command = [*stable, "core", "print", input_path, "--input-format", "source"]
    elif kind == "v1-run":
        command = [*stable, "run", input_path]
    elif kind == "v1-native":
        command = []
    else:
        raise AssertionError(kind)
    if kind == "core-eval":
        command.extend(("--symbol", case.fields["symbol"]))
        if "fuel" in case.fields:
            command.extend(("--fuel", case.fields["fuel"]))
    environment = dict(os.environ)
    repository = Path(__file__).resolve().parents[4]
    source = str(repository / "src" / "sloph-python-bootstrap")
    environment["PYTHONPATH"] = source + os.pathsep + environment.get("PYTHONPATH", "")
    if kind == "v1-native":
        with tempfile.TemporaryDirectory(prefix="sloph-case-") as directory:
            executable = str(Path(directory) / "program")
            compiled = subprocess.run(
                [*stable, "compile", input_path, "-o", executable],
                cwd=case.directory, env=environment, check=False,
                capture_output=True, text=True, encoding="utf-8", timeout=30,
            )
            if compiled.returncode != 0:
                return Outcome(compiled.returncode, compiled.stdout, compiled.stderr)
            executed = subprocess.run(
                [executable], cwd=case.directory, env=environment,
                check=False, capture_output=True, text=True, encoding="utf-8",
                timeout=30,
            )
            return Outcome(executed.returncode, executed.stdout, executed.stderr)
    if kind == "core-run":
        with tempfile.TemporaryDirectory(prefix="sloph-case-") as directory:
            executable = str(Path(directory) / "program")
            reference = subprocess.run(
                [*prefix, "core", "eval", input_path, "--symbol", case.fields["symbol"]],
                cwd=case.directory, env=environment, check=False,
                capture_output=True, text=True, encoding="utf-8", timeout=30,
            )
            if reference.returncode != 0:
                return Outcome(reference.returncode, reference.stdout, reference.stderr)
            compile_command = [
                *prefix, "compile", input_path, "--input-format", "text",
                "--symbol", case.fields["symbol"], "-o", executable,
            ]
            compiled = subprocess.run(
                compile_command, cwd=case.directory, env=environment,
                check=False, capture_output=True, text=True, encoding="utf-8",
                timeout=30,
            )
            if compiled.returncode != 0:
                return Outcome(compiled.returncode, compiled.stdout, compiled.stderr)
            executed = subprocess.run(
                [executable], cwd=case.directory, env=environment,
                check=False, capture_output=True, text=True, encoding="utf-8",
                timeout=30,
            )
            if executed.returncode == 0 and executed.stdout != reference.stdout:
                return Outcome(
                    1,
                    executed.stdout,
                    "native output differs from the Core reference evaluator\n",
                )
            return Outcome(executed.returncode, executed.stdout, executed.stderr)
    completed = subprocess.run(
        command,
        cwd=case.directory,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=30,
    )
    return Outcome(completed.returncode, completed.stdout, completed.stderr)

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import subprocess
import sys


REQUIRED = {"format", "name", "kind", "input", "expect-exit"}
OPTIONAL = {"symbol", "fuel", "expect-output", "expect-diagnostics"}
KINDS = {"core-check", "core-print", "core-eval"}


@dataclass(frozen=True)
class Case:
    directory: Path
    fields: dict[str, str]

    def path(self, field: str) -> Path:
        candidate = (self.directory / self.fields[field]).resolve()
        if self.directory.resolve() not in candidate.parents:
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
    if expected_exit not in range(5):
        raise ValueError(f"{path}: invalid expect-exit")
    if fields["kind"] != "core-eval" and ({"symbol", "fuel"} & fields.keys()):
        raise ValueError(f"{path}: symbol and fuel are only valid for core-eval")
    if fields["kind"] == "core-eval" and "symbol" not in fields:
        raise ValueError(f"{path}: core-eval requires symbol")
    if "fuel" in fields:
        try:
            if int(fields["fuel"]) <= 0:
                raise ValueError
        except ValueError as error:
            raise ValueError(f"{path}: fuel must be a positive integer") from error
    case = Case(path.parent.resolve(), fields)
    for field in ("input", "expect-output", "expect-diagnostics"):
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
    action = {"core-check": "check", "core-print": "print", "core-eval": "eval"}[kind]
    command = [
        sys.executable,
        "-m",
        "sloph",
        "--diagnostics",
        "jsonl",
        "unstable",
        "core",
        action,
        str(case.path("input")),
    ]
    if kind == "core-eval":
        command.extend(("--symbol", case.fields["symbol"]))
        if "fuel" in case.fields:
            command.extend(("--fuel", case.fields["fuel"]))
    environment = dict(os.environ)
    source = str(Path(__file__).resolve().parents[2] / "src")
    environment["PYTHONPATH"] = source + os.pathsep + environment.get("PYTHONPATH", "")
    completed = subprocess.run(
        command,
        cwd=case.directory,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=10,
    )
    return Outcome(completed.returncode, completed.stdout, completed.stderr)

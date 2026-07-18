from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


COMPONENT = Path(__file__).resolve().parents[1]
REPOSITORY = COMPONENT.parents[1]
EXAMPLES = REPOSITORY / "examples"
REQUIRED_FILES = ("README.md", "sloph.toml", "expected.stdout")


@dataclass(frozen=True, slots=True)
class Example:
    name: str
    directory: Path
    sources: tuple[Path, ...]


def discover_examples(root: Path) -> tuple[Example, ...]:
    if not root.is_dir():
        raise ValueError(f"missing examples directory: {root}")
    examples: list[Example] = []
    for directory in sorted(path for path in root.iterdir() if path.is_dir()):
        missing = [name for name in REQUIRED_FILES if not (directory / name).is_file()]
        if missing:
            raise ValueError(f"{directory.name}: missing required files {missing}")
        source_root = directory / "src"
        sources = tuple(sorted(source_root.rglob("*.sloph"))) if source_root.is_dir() else ()
        if not sources:
            raise ValueError(f"{directory.name}: requires at least one src/*.sloph file")
        examples.append(Example(directory.name, directory, sources))
    if not examples:
        raise ValueError("examples directory contains no examples")
    return tuple(examples)


def run_cli(*arguments: str, timeout: int = 30) -> subprocess.CompletedProcess[bytes]:
    environment = dict(os.environ)
    source = str(COMPONENT)
    environment["PYTHONPATH"] = source + os.pathsep + environment.get("PYTHONPATH", "")
    return subprocess.run(
        [sys.executable, "-m", "sloph", *arguments],
        cwd=REPOSITORY,
        env=environment,
        check=False,
        capture_output=True,
        timeout=timeout,
    )


class ExampleTests(unittest.TestCase):
    def test_all_examples_check_format_and_run(self) -> None:
        for example in discover_examples(EXAMPLES):
            with self.subTest(example=example.name, operation="check"):
                checked = run_cli("check", str(example.directory))
                self.assertEqual(b"", checked.stdout)
                self.assertEqual(b"", checked.stderr)
                self.assertEqual(0, checked.returncode)

            for source in example.sources:
                with self.subTest(
                    example=example.name,
                    operation="format",
                    source=source.relative_to(example.directory).as_posix(),
                ):
                    formatted = run_cli("format", "--check", str(source))
                    self.assertEqual(b"", formatted.stdout)
                    self.assertEqual(b"", formatted.stderr)
                    self.assertEqual(0, formatted.returncode)

            with self.subTest(example=example.name, operation="run"):
                executed = run_cli("run", str(example.directory))
                expected = (example.directory / "expected.stdout").read_bytes()
                self.assertEqual(expected, executed.stdout)
                self.assertEqual(b"", executed.stderr)
                self.assertEqual(0, executed.returncode)

    def test_discovery_rejects_incomplete_examples(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sloph-examples-test-") as directory:
            root = Path(directory)
            incomplete = root / "incomplete"
            incomplete.mkdir()
            (incomplete / "README.md").write_text("# Incomplete\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "missing required files"):
                discover_examples(root)


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

from pathlib import Path

from sloph.core.diagnostics import fail


def libraries_root() -> Path:
    """Return the monorepo's bundled language-library directory."""

    root = Path(__file__).resolve().parents[2] / "libraries"
    if all(
        (root / package / "library.json").is_file()
        for package in ("core", "os", "std", "syscall")
    ):
        return root
    fail(
        "toolchain.libraries.missing",
        "environment",
        "the Python bootstrap requires the monorepo src/libraries directory",
        expected=str(root),
    )


__all__ = ["libraries_root"]

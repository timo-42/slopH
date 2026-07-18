from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys
from typing import Sequence

from sloph import __version__
from sloph.core import (
    Diagnostic,
    DiagnosticError,
    Limits,
    evaluate,
    format_core,
    format_value,
    parse_core,
    validate,
)
from sloph.core.diagnostics import Span, limit_fail


class _UsageError(Exception):
    pass


class _ArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise _UsageError(message)


def _parser() -> argparse.ArgumentParser:
    parser = _ArgumentParser(prog="sloph")
    parser.add_argument(
        "--diagnostics",
        choices=("human", "jsonl"),
        default="human",
        help="diagnostic rendering (default: human)",
    )
    parser.add_argument("--version", action="version", version=f"sloph {__version__}")
    commands = parser.add_subparsers(dest="command", required=True)
    unstable = commands.add_parser("unstable", help="unstable implementation tools")
    unstable_commands = unstable.add_subparsers(dest="unstable_command", required=True)
    core = unstable_commands.add_parser("core", help="experimental Core v0 tools")
    core_commands = core.add_subparsers(dest="core_command", required=True)

    check = core_commands.add_parser("check", help="parse and validate Core v0")
    check.add_argument("input", metavar="INPUT")

    print_command = core_commands.add_parser(
        "print", help="print canonical Core v0 text"
    )
    print_command.add_argument("input", metavar="INPUT")
    print_command.add_argument("-o", "--output", default="-")

    eval_command = core_commands.add_parser(
        "eval", help="evaluate a Core v0 global definition"
    )
    eval_command.add_argument("input", metavar="INPUT")
    eval_command.add_argument("--symbol", required=True, metavar="GLOBAL_ID")
    eval_command.add_argument("--fuel", type=_positive_integer, metavar="NUMBER")
    return parser


def _positive_integer(value: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a decimal integer") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def _read_input(path: str, limits: Limits) -> bytes:
    if path == "-":
        data = sys.stdin.buffer.read(limits.input_bytes + 1)
    else:
        with Path(path).open("rb") as stream:
            data = stream.read(limits.input_bytes + 1)
    if len(data) > limits.input_bytes:
        limit_fail(
            "parse",
            "input_bytes",
            limits.input_bytes,
            Span(0, len(data)),
        )
    return data


def _write_output(path: str, content: str) -> None:
    if path == "-":
        sys.stdout.write(content)
        return
    destination = Path(path)
    temporary = destination.with_name(
        f".{destination.name}.tmp-{os.getpid()}"
    )
    try:
        temporary.write_text(content, encoding="ascii", newline="\n")
        temporary.replace(destination)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def _run(args: argparse.Namespace) -> int:
    limits = Limits()
    if getattr(args, "fuel", None) is not None:
        limits = limits.with_fuel(args.fuel)
    data = _read_input(args.input, limits)
    unit = parse_core(data, limits)
    if args.core_command == "check":
        validate(unit)
        return 0
    if args.core_command == "print":
        _write_output(args.output, format_core(unit, limits))
        return 0
    if args.core_command == "eval":
        value = evaluate(unit, args.symbol, limits)
        sys.stdout.write(format_value(value, limits))
        return 0
    raise AssertionError(f"unsupported command {args.core_command!r}")


def main(argv: Sequence[str] | None = None) -> int:
    parser = _parser()
    raw_arguments = list(sys.argv[1:] if argv is None else argv)
    try:
        args = parser.parse_args(raw_arguments)
    except _UsageError as error:
        jsonl = any(
            item == "--diagnostics=jsonl"
            or (
                item == "--diagnostics"
                and index + 1 < len(raw_arguments)
                and raw_arguments[index + 1] == "jsonl"
            )
            for index, item in enumerate(raw_arguments)
        )
        diagnostic = Diagnostic("tool.usage", "cli", str(error))
        if jsonl:
            sys.stderr.write(diagnostic.json_line() + "\n")
        else:
            sys.stderr.write(diagnostic.human("<command-line>") + "\n")
        return 2
    try:
        return _run(args)
    except DiagnosticError as error:
        if args.diagnostics == "jsonl":
            sys.stderr.write(error.diagnostic.json_line() + "\n")
        else:
            sys.stderr.write(error.diagnostic.human(args.input) + "\n")
        return 1
    except (OSError, UnicodeError) as error:
        diagnostic = Diagnostic(
            "tool.io",
            "environment",
            str(error),
            details={"input": getattr(args, "input", None)},
        )
        if args.diagnostics == "jsonl":
            sys.stderr.write(diagnostic.json_line() + "\n")
        else:
            sys.stderr.write(diagnostic.human(getattr(args, "input", "<input>")) + "\n")
        return 3
    except Exception as error:
        diagnostic = Diagnostic(
            "tool.internal",
            "internal",
            f"internal compiler failure: {type(error).__name__}",
            details={"exception": type(error).__name__},
        )
        if args.diagnostics == "jsonl":
            sys.stderr.write(diagnostic.json_line() + "\n")
        else:
            sys.stderr.write(diagnostic.human(getattr(args, "input", "<input>")) + "\n")
        return 4


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

import argparse
import os
from pathlib import Path
import json
import sys
import tempfile
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
from sloph.core.diagnostics import Span, fail, limit_fail


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
    print_command.add_argument(
        "--input-format", choices=("text", "source"), default="text"
    )
    print_command.add_argument("-o", "--output", default="-")

    eval_command = core_commands.add_parser(
        "eval", help="evaluate a Core v0 global definition"
    )
    eval_command.add_argument("input", metavar="INPUT")
    eval_command.add_argument("--symbol", required=True, metavar="GLOBAL_ID")
    eval_command.add_argument("--fuel", type=_positive_integer, metavar="NUMBER")

    source_check = unstable_commands.add_parser(
        "check", help="check an experimental Source v0 project"
    )
    source_check.add_argument("input", metavar="PROJECT")

    source_format = unstable_commands.add_parser(
        "format", help="format an experimental Source v0 file"
    )
    source_format.add_argument("input", metavar="INPUT")
    format_mode = source_format.add_mutually_exclusive_group()
    format_mode.add_argument("--write", action="store_true")
    format_mode.add_argument("--check", action="store_true")
    format_mode.add_argument("--stdout", action="store_true")

    ast = unstable_commands.add_parser("ast", help="public Source v0 AST tools")
    ast_commands = ast.add_subparsers(dest="ast_command", required=True)
    ast_print = ast_commands.add_parser("print", help="print Source v0 AST JSON")
    ast_print.add_argument("input", metavar="INPUT")
    ast_print.add_argument("--input-format", choices=("source", "json"), default="source")
    ast_print.add_argument("--format", choices=("json",), default="json")
    ast_print.add_argument("-o", "--output", default="-")
    ast_check = ast_commands.add_parser("check", help="validate Source v0 or AST JSON")
    ast_check.add_argument("input", metavar="INPUT")
    ast_check.add_argument("--input-format", choices=("source", "json"), default="source")

    compile_command = unstable_commands.add_parser(
        "compile", help="compile Source or first-order Core through C11"
    )
    compile_command.add_argument("input", metavar="INPUT")
    compile_command.add_argument("--input-format", choices=("source", "text"), default="source")
    compile_command.add_argument("--symbol", metavar="GLOBAL_ID")
    compile_command.add_argument("-o", "--output", required=True)
    compile_command.add_argument("--cc", default="cc", metavar="PATH")
    compile_command.add_argument("--emit-c", metavar="PATH")
    compile_command.add_argument("--timings", action="store_true")

    run_command = unstable_commands.add_parser(
        "run", help="compile and run a Source v0 project"
    )
    run_command.add_argument("input", metavar="PROJECT")
    run_command.add_argument("--cc", default="cc", metavar="PATH")
    run_command.add_argument("--emit-c", metavar="PATH")
    run_command.add_argument("--timings", action="store_true")

    stable_check = commands.add_parser("check", help="check a Source v1 project")
    stable_check.add_argument("input", metavar="PROJECT")

    stable_format = commands.add_parser("format", help="format a Source v1 file")
    stable_format.add_argument("input", metavar="INPUT")
    stable_format_mode = stable_format.add_mutually_exclusive_group()
    stable_format_mode.add_argument("--write", action="store_true")
    stable_format_mode.add_argument("--check", action="store_true")
    stable_format_mode.add_argument("--stdout", action="store_true")

    stable_ast = commands.add_parser("ast", help="public Source v1 AST tools")
    stable_ast_commands = stable_ast.add_subparsers(dest="ast_command", required=True)
    stable_ast_print = stable_ast_commands.add_parser("print", help="print Source v1 AST JSON")
    stable_ast_print.add_argument("input", metavar="INPUT")
    stable_ast_print.add_argument("--input-format", choices=("source", "json"), default="source")
    stable_ast_print.add_argument("--format", choices=("json",), default="json")
    stable_ast_print.add_argument("-o", "--output", default="-")
    stable_ast_check = stable_ast_commands.add_parser("check", help="validate Source v1 or AST JSON")
    stable_ast_check.add_argument("input", metavar="INPUT")
    stable_ast_check.add_argument("--input-format", choices=("source", "json"), default="source")

    stable_core = commands.add_parser("core", help="public Core v2/v3 tools")
    stable_core_commands = stable_core.add_subparsers(dest="core_command", required=True)
    stable_core_check = stable_core_commands.add_parser("check", help="validate Core v2/v3")
    stable_core_check.add_argument("input", metavar="INPUT")
    stable_core_check.add_argument("--input-format", choices=("text", "source"), default="text")
    stable_core_print = stable_core_commands.add_parser("print", help="print canonical Core v2/v3")
    stable_core_print.add_argument("input", metavar="INPUT")
    stable_core_print.add_argument("--input-format", choices=("text", "source"), default="source")
    stable_core_print.add_argument("-o", "--output", default="-")

    stable_compile = commands.add_parser("compile", help="compile Source v1 through C11")
    stable_compile.add_argument("input", metavar="PROJECT")
    stable_compile.add_argument("-o", "--output", required=True)
    stable_compile.add_argument("--cc", default="cc", metavar="PATH")
    stable_compile.add_argument("--emit-c", metavar="PATH")
    stable_compile.add_argument("--timings", action="store_true")

    stable_run = commands.add_parser("run", help="compile and run a Source v1 project")
    stable_run.add_argument("input", metavar="PROJECT")
    stable_run.add_argument("--cc", default="cc", metavar="PATH")
    stable_run.add_argument("--emit-c", metavar="PATH")
    stable_run.add_argument("--timings", action="store_true")
    return parser


def _positive_integer(value: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a decimal integer") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def _read_input(path: str, limits: Limits, *, profile: str = "core") -> bytes:
    if path == "-":
        data = sys.stdin.buffer.read(limits.input_bytes + 1)
    else:
        with Path(path).open("rb") as stream:
            data = stream.read(limits.input_bytes + 1)
    if len(data) > limits.input_bytes:
        if profile == "core":
            limit_fail("parse", "input_bytes", limits.input_bytes, Span(0, len(data)))
        fail(
            "syntax.parse.limit_exceeded",
            "parse",
            f"input_bytes limit exceeded (configured {limits.input_bytes})",
            Span(0, len(data)),
            limit="input_bytes",
            configured=limits.input_bytes,
        )
    return data


def _write_output(path: str, content: str, *, preserve_mode: bool = False) -> None:
    if path == "-":
        sys.stdout.write(content)
        return
    destination = Path(path)
    mode = destination.stat().st_mode & 0o7777 if preserve_mode and destination.exists() else None
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.tmp-", dir=destination.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="ascii", newline="\n") as stream:
            stream.write(content)
        if mode is not None:
            temporary.chmod(mode)
        temporary.replace(destination)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def _run(args: argparse.Namespace) -> int:
    if args.command != "unstable":
        return _run_v1(args)
    limits = Limits()
    if getattr(args, "fuel", None) is not None:
        limits = limits.with_fuel(args.fuel)
    if args.unstable_command == "core":
        if args.core_command in ("check", "eval"):
            data = _read_input(args.input, limits)
            unit = parse_core(data, limits)
        elif args.core_command == "print" and getattr(args, "input_format", "text") == "source":
            from sloph.project import elaborate_project
            unit = elaborate_project(args.input, limits)
        else:
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
    if args.unstable_command == "check":
        from sloph.project import elaborate_project
        elaborate_project(args.input, limits)
        return 0
    if args.unstable_command == "format":
        from sloph.syntax import format_source, parse_source
        data = _read_input(args.input, limits, profile="syntax")
        rendered = format_source(parse_source(data, limits), limits)
        if args.write:
            if args.input == "-":
                raise _UsageError("--write requires a file input")
            _write_output(args.input, rendered, preserve_mode=True)
        elif args.check:
            return 0 if data == rendered.encode("ascii") else 1
        else:
            sys.stdout.write(rendered)
        return 0
    if args.unstable_command == "ast":
        from sloph.syntax import parse_source, syntax_from_json, syntax_to_json
        data = _read_input(args.input, limits, profile="syntax")
        module = (
            parse_source(data, limits)
            if args.input_format == "source"
            else syntax_from_json(data, limits)
        )
        if args.ast_command == "print":
            _write_output(args.output, syntax_to_json(module, limits))
        return 0
    if args.unstable_command == "compile":
        from sloph.compiler import compile_core, compile_project
        if args.input_format == "source":
            if args.symbol is not None:
                raise _UsageError("--symbol is only valid with --input-format text")
            result = compile_project(
                args.input, args.output, cc=args.cc, emit_c_path=args.emit_c
            )
        else:
            if args.symbol is None:
                raise _UsageError("--symbol is required with --input-format text")
            unit = parse_core(_read_input(args.input, limits), limits)
            result = compile_core(
                unit, args.symbol, args.output, cc=args.cc, emit_c_path=args.emit_c
            )
        if args.timings:
            _render_timings(result.timings_ns, args.diagnostics)
        return 0
    if args.unstable_command == "run":
        from sloph.compiler import run_project
        result, completed = run_project(
            args.input, cc=args.cc, emit_c_path=args.emit_c
        )
        if args.timings:
            _render_timings(result.timings_ns, args.diagnostics)
        if completed.returncode == 0:
            sys.stdout.buffer.write(completed.stdout)
            sys.stderr.buffer.write(completed.stderr)
            return 0
        if args.diagnostics == "jsonl":
            diagnostic = Diagnostic(
                "compiler.runtime.failed",
                "runtime",
                "compiled program failed",
                details={
                    "exit_code": completed.returncode,
                    "stderr": completed.stderr.decode("utf-8", errors="replace")[-65_536:],
                },
            )
            sys.stderr.write(diagnostic.json_line() + "\n")
        else:
            sys.stdout.buffer.write(completed.stdout)
            sys.stderr.buffer.write(completed.stderr)
        return 1
    raise AssertionError(f"unsupported unstable command {args.unstable_command!r}")


def _run_v1(args: argparse.Namespace) -> int:
    limits = Limits()
    if args.command == "check":
        from sloph.project import elaborate_project_v1
        elaborate_project_v1(args.input, limits)
        return 0
    if args.command == "format":
        from sloph.syntax import format_source, parse_source_v1
        data = _read_input(args.input, limits, profile="syntax")
        rendered = format_source(parse_source_v1(data, limits), limits, version=1)
        if args.write:
            if args.input == "-":
                raise _UsageError("--write requires a file input")
            _write_output(args.input, rendered, preserve_mode=True)
        elif args.check:
            return 0 if data == rendered.encode("ascii") else 1
        else:
            sys.stdout.write(rendered)
        return 0
    if args.command == "ast":
        from sloph.syntax import parse_source_v1, syntax_from_json, syntax_to_json
        data = _read_input(args.input, limits, profile="syntax")
        module = (
            parse_source_v1(data, limits)
            if args.input_format == "source"
            else syntax_from_json(data, limits, version=1)
        )
        if args.ast_command == "print":
            _write_output(args.output, syntax_to_json(module, limits, version=1))
        return 0
    if args.command == "core":
        from sloph.project import elaborate_project_v1
        if args.input_format == "source":
            unit = elaborate_project_v1(args.input, limits)
        else:
            unit = parse_core(_read_input(args.input, limits), limits)
            if unit.version not in (2, 3):
                fail(
                    "core.validate.unsupported_version",
                    "validate",
                    "stable Core commands require Core version 2 or 3",
                    unit.span,
                    version=unit.version,
                )
        validate(unit)
        if args.core_command == "print":
            _write_output(args.output, format_core(unit, limits))
        return 0
    if args.command == "compile":
        from sloph.compiler import compile_project
        result = compile_project(
            args.input,
            args.output,
            cc=args.cc,
            emit_c_path=args.emit_c,
            source_version=1,
        )
        if args.timings:
            _render_timings(result.timings_ns, args.diagnostics)
        return 0
    if args.command == "run":
        from sloph.compiler import run_project
        result, completed = run_project(
            args.input,
            cc=args.cc,
            emit_c_path=args.emit_c,
            source_version=1,
        )
        if args.timings:
            _render_timings(result.timings_ns, args.diagnostics)
        sys.stdout.buffer.write(completed.stdout)
        sys.stderr.buffer.write(completed.stderr)
        return 0 if completed.returncode == 0 else 1
    raise AssertionError(f"unsupported stable command {args.command!r}")


def _render_timings(values: dict[str, int], diagnostics: str) -> None:
    for phase in sorted(values):
        if diagnostics == "jsonl":
            sys.stderr.write(
                json.dumps(
                    {
                        "schema": "sloph.timing",
                        "version": 0,
                        "phase": phase,
                        "nanoseconds": values[phase],
                    },
                    sort_keys=True,
                    separators=(",", ":"),
                )
                + "\n"
            )
        else:
            sys.stderr.write(f"timing {phase}: {values[phase]} ns\n")


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
    except _UsageError as error:
        diagnostic = Diagnostic("tool.usage", "cli", str(error))
        if args.diagnostics == "jsonl":
            sys.stderr.write(diagnostic.json_line() + "\n")
        else:
            sys.stderr.write(diagnostic.human("<command-line>") + "\n")
        return 2
    except DiagnosticError as error:
        if args.diagnostics == "jsonl":
            sys.stderr.write(error.diagnostic.json_line() + "\n")
        else:
            sys.stderr.write(error.diagnostic.human(args.input) + "\n")
        return 3 if error.diagnostic.phase == "environment" else 1
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

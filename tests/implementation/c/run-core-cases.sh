#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
compiler=${SLOPH_C_COMPILER:-$repository/src/sloph-c-bootstrap/build/bin/sloph}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/sloph-c-cases.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

if [ ! -x "$compiler" ]; then
    echo "C compiler not found: $compiler" >&2
    echo "run: make -C $repository/src/sloph-c-bootstrap" >&2
    exit 2
fi

passed=0
failed=0
for description in $(find "$repository/tests/core" -name case.test -print | sort); do
    kind=
    name=
    input=
    symbol=
    fuel=
    expected_exit=
    expected_output=
    expected_diagnostics=
    while IFS=: read -r key value; do
        value=$(printf '%s' "$value" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
        case "$key" in
            kind) kind=$value ;;
            name) name=$value ;;
            input) input=$value ;;
            symbol) symbol=$value ;;
            fuel) fuel=$value ;;
            expect-exit) expected_exit=$value ;;
            expect-output) expected_output=$value ;;
            expect-diagnostics) expected_diagnostics=$value ;;
        esac
    done < "$description"
    case "$kind" in
        core-check|core-print|core-eval) ;;
        *) continue ;;
    esac
    case_dir=$(dirname "$description")
    stdout_file="$temporary/stdout"
    stderr_file="$temporary/stderr"
    empty_file="$temporary/empty"
    : > "$empty_file"
    set -- "$compiler" --diagnostics jsonl unstable core
    case "$kind" in
        core-check) set -- "$@" check "$case_dir/$input" ;;
        core-print) set -- "$@" print "$case_dir/$input" ;;
        core-eval)
            set -- "$@" eval "$case_dir/$input" --symbol "$symbol"
            if [ -n "$fuel" ]; then set -- "$@" --fuel "$fuel"; fi
            ;;
    esac
    actual_exit=0
    "$@" >"$stdout_file" 2>"$stderr_file" || actual_exit=$?
    output_reference=$empty_file
    diagnostic_reference=$empty_file
    if [ -n "$expected_output" ]; then output_reference="$case_dir/$expected_output"; fi
    if [ -n "$expected_diagnostics" ]; then
        diagnostic_reference="$case_dir/$expected_diagnostics"
    fi
    if [ "$actual_exit" = "$expected_exit" ] &&
       cmp -s "$output_reference" "$stdout_file" &&
       cmp -s "$diagnostic_reference" "$stderr_file"; then
        passed=$((passed + 1))
        printf 'ok %s\n' "$name"
    else
        failed=$((failed + 1))
        printf 'FAIL %s (exit %s, expected %s)\n' "$name" "$actual_exit" "$expected_exit" >&2
        if ! cmp -s "$output_reference" "$stdout_file"; then
            diff -u "$output_reference" "$stdout_file" >&2 || true
        fi
        if ! cmp -s "$diagnostic_reference" "$stderr_file"; then
            diff -u "$diagnostic_reference" "$stderr_file" >&2 || true
        fi
    fi
done

printf '%s Core cases passed; %s failed\n' "$passed" "$failed"
test "$failed" -eq 0

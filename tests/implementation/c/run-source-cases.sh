#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
compiler=${SLOPH_C_COMPILER:-$repository/src/sloph-c-bootstrap/build/bin/sloph}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/sloph-c-source-cases.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

if [ ! -x "$compiler" ]; then
    echo "C compiler not found: $compiler" >&2
    echo "run: make -C $repository/src/sloph-c-bootstrap" >&2
    exit 2
fi

passed=0
failed=0
for description in $(find "$repository/tests/source" "$repository/tests/v1" \
    -name case.test -print | sort); do
    kind=
    name=
    input=
    expected_exit=
    expected_output=
    expected_diagnostics=
    while IFS=: read -r key value; do
        value=$(printf '%s' "$value" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
        case "$key" in
            kind) kind=$value ;;
            name) name=$value ;;
            input) input=$value ;;
            expect-exit) expected_exit=$value ;;
            expect-output) expected_output=$value ;;
            expect-diagnostics) expected_diagnostics=$value ;;
        esac
    done < "$description"
    case "$kind" in
        source-check|source-format|source-ast|source-core|v1-check|v1-format|v1-ast|v1-core) ;;
        *) continue ;;
    esac
    case_dir=$(dirname "$description")
    input_path=$case_dir/$input
    stdout_file=$temporary/stdout
    stderr_file=$temporary/stderr
    empty_file=$temporary/empty
    : > "$empty_file"
    set -- "$compiler" --diagnostics jsonl
    case "$kind" in
        source-check) set -- "$@" unstable check "$input_path" ;;
        source-format) set -- "$@" unstable format "$input_path" --stdout ;;
        source-ast) set -- "$@" unstable ast print "$input_path" --format json ;;
        source-core)
            set -- "$@" unstable core print "$input_path" --input-format source ;;
        v1-check) set -- "$@" check "$input_path" ;;
        v1-format) set -- "$@" format "$input_path" --stdout ;;
        v1-ast) set -- "$@" ast print "$input_path" --format json ;;
        v1-core) set -- "$@" core print "$input_path" --input-format source ;;
    esac
    actual_exit=0
    (cd "$case_dir" && "$@") >"$stdout_file" 2>"$stderr_file" || actual_exit=$?
    output_reference=$empty_file
    diagnostic_reference=$empty_file
    if [ -n "$expected_output" ]; then output_reference=$case_dir/$expected_output; fi
    if [ -n "$expected_diagnostics" ]; then
        diagnostic_reference=$case_dir/$expected_diagnostics
    fi
    if [ "$actual_exit" = "$expected_exit" ] &&
       cmp -s "$output_reference" "$stdout_file" &&
       cmp -s "$diagnostic_reference" "$stderr_file"; then
        passed=$((passed + 1))
        printf 'ok %s\n' "$name"
    else
        failed=$((failed + 1))
        printf 'FAIL %s (exit %s, expected %s)\n' \
            "$name" "$actual_exit" "$expected_exit" >&2
        if ! cmp -s "$output_reference" "$stdout_file"; then
            diff -u "$output_reference" "$stdout_file" >&2 || true
        fi
        if ! cmp -s "$diagnostic_reference" "$stderr_file"; then
            diff -u "$diagnostic_reference" "$stderr_file" >&2 || true
        fi
    fi
done

printf '%s Source cases passed; %s failed\n' "$passed" "$failed"
test "$failed" -eq 0

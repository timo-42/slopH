#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
compiler=${SLOPH_C_COMPILER:-$repository/src/sloph-c-bootstrap/build/bin/sloph}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/sloph-c-native-cases.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

if [ ! -x "$compiler" ]; then
    echo "C compiler not found: $compiler" >&2
    echo "run: make -C $repository/src/sloph-c-bootstrap" >&2
    exit 2
fi

passed=0
failed=0
for description in $(find "$repository/tests/core/native" \
    "$repository/tests/source/run" "$repository/tests/v1/run" \
    -name case.test -print | sort); do
    kind=
    name=
    input=
    symbol=
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
            expect-exit) expected_exit=$value ;;
            expect-output) expected_output=$value ;;
            expect-diagnostics) expected_diagnostics=$value ;;
        esac
    done < "$description"
    case "$kind" in core-run|source-run|v1-run|v1-native) ;; *) continue ;; esac
    case_dir=$(dirname "$description")
    input_path=$case_dir/$input
    stdout_file=$temporary/stdout
    stderr_file=$temporary/stderr
    executable=$temporary/program
    empty_file=$temporary/empty
    : > "$empty_file"
    actual_exit=0
    case "$kind" in
        core-run)
            reference=$temporary/reference
            (cd "$case_dir" && "$compiler" --diagnostics jsonl unstable core eval \
                "$input_path" --symbol "$symbol") >"$reference" 2>"$stderr_file" || actual_exit=$?
            if [ "$actual_exit" = 0 ]; then
                (cd "$case_dir" && "$compiler" --diagnostics jsonl unstable compile \
                    "$input_path" --input-format text --symbol "$symbol" -o "$executable") \
                    >"$stdout_file" 2>"$stderr_file" || actual_exit=$?
            fi
            if [ "$actual_exit" = 0 ]; then
                (cd "$case_dir" && "$executable") >"$stdout_file" 2>"$stderr_file" || actual_exit=$?
            fi
            if [ "$actual_exit" = 0 ] && ! cmp -s "$reference" "$stdout_file"; then
                printf '%s\n' 'native output differs from the Core reference evaluator' >"$stderr_file"
                actual_exit=1
            fi ;;
        source-run)
            (cd "$case_dir" && "$compiler" --diagnostics jsonl unstable run "$input_path") \
                >"$stdout_file" 2>"$stderr_file" || actual_exit=$? ;;
        v1-run)
            (cd "$case_dir" && "$compiler" --diagnostics jsonl run "$input_path") \
                >"$stdout_file" 2>"$stderr_file" || actual_exit=$? ;;
        v1-native)
            (cd "$case_dir" && "$compiler" --diagnostics jsonl compile "$input_path" \
                -o "$executable") >"$stdout_file" 2>"$stderr_file" || actual_exit=$?
            if [ "$actual_exit" = 0 ]; then
                (cd "$case_dir" && "$executable") >"$stdout_file" 2>"$stderr_file" || actual_exit=$?
            fi ;;
    esac
    output_reference=$empty_file
    diagnostic_reference=$empty_file
    if [ -n "$expected_output" ]; then output_reference=$case_dir/$expected_output; fi
    if [ -n "$expected_diagnostics" ]; then diagnostic_reference=$case_dir/$expected_diagnostics; fi
    if [ "$actual_exit" = "$expected_exit" ] &&
       cmp -s "$output_reference" "$stdout_file" &&
       cmp -s "$diagnostic_reference" "$stderr_file"; then
        passed=$((passed + 1)); printf 'ok %s\n' "$name"
    else
        failed=$((failed + 1))
        printf 'FAIL %s (exit %s, expected %s)\n' "$name" "$actual_exit" "$expected_exit" >&2
        diff -u "$output_reference" "$stdout_file" >&2 || true
        diff -u "$diagnostic_reference" "$stderr_file" >&2 || true
    fi
done

printf '%s native cases passed; %s failed\n' "$passed" "$failed"
test "$failed" -eq 0

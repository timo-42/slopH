#!/bin/sh
set -eu

compiler=${SLOPH_C_COMPILER:?SLOPH_C_COMPILER is required}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/sloph-cli-test.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

"$compiler" --help >"$temporary/actual" 2>"$temporary/stderr"
cat >"$temporary/expected" <<'EOF'
usage: sloph [-h] [--diagnostics {human,jsonl}] [--version]
             {unstable,canopy-to-crown,crown-to-heartwood,check,format,ast,core,compile,run} ...

positional arguments:
  {unstable,canopy-to-crown,crown-to-heartwood,check,format,ast,core,compile,run}
    unstable            unstable implementation tools
    canopy-to-crown     transform Source canopy into Crown AST JSON
    crown-to-heartwood  transform a Crown project into canonical Heartwood Core
    check               check a Source v1 project
    format              format a Source v1 file
    ast                 public Source v1 AST tools
    core                public Core v2/v3 tools
    compile             compile Source v1 through C11
    run                 compile and run a Source v1 project

options:
  -h, --help            show this help message and exit
  --diagnostics {human,jsonl}
                        diagnostic rendering (default: human)
  --version             show program's version number and exit
EOF
cmp "$temporary/expected" "$temporary/actual"
test ! -s "$temporary/stderr"

"$compiler" canopy-to-crown --help >"$temporary/actual"
grep -q '^usage: sloph canopy-to-crown ' "$temporary/actual"
grep -q 'Crown AST JSON' "$temporary/actual"
"$compiler" crown-to-heartwood --help >"$temporary/actual"
grep -q '^usage: sloph crown-to-heartwood ' "$temporary/actual"
grep -q 'canonical Heartwood Core' "$temporary/actual"

"$compiler" --version >"$temporary/actual" 2>"$temporary/stderr"
printf 'sloph 0.0.0\n' >"$temporary/expected"
cmp "$temporary/expected" "$temporary/actual"
test ! -s "$temporary/stderr"

missing=/definitely/sloph-cli-missing-input
actual_exit=0
"$compiler" --diagnostics jsonl unstable ast print "$missing" \
    >"$temporary/stdout" 2>"$temporary/actual" || actual_exit=$?
test "$actual_exit" -eq 3
test ! -s "$temporary/stdout"
printf '%s\n' '{"code":"tool.io","details":{"input":"/definitely/sloph-cli-missing-input"},"message":"[Errno 2] No such file or directory: '\''/definitely/sloph-cli-missing-input'\''","message_id":"tool.io","phase":"environment","schema":"sloph.diagnostic","severity":"error","span":{"end":0,"start":0},"version":0}' >"$temporary/expected"
cmp "$temporary/expected" "$temporary/actual"

actual_exit=0
"$compiler" --diagnostics jsonl check "$missing" \
    >"$temporary/stdout" 2>"$temporary/actual" || actual_exit=$?
test "$actual_exit" -eq 3
cmp "$temporary/expected" "$temporary/actual"

repository=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source_file=$repository/tests/source/format/basic/input.sloph
"$compiler" unstable format --stdout "$source_file" >"$temporary/actual"
"$compiler" unstable format "$source_file" --stdout >"$temporary/expected"
cmp "$temporary/expected" "$temporary/actual"

ast_source=$repository/tests/v1/ast/standard-transform/input.sloph
"$compiler" canopy-to-crown "$ast_source" >"$temporary/actual"
"$compiler" ast print "$ast_source" --format json >"$temporary/expected"
cmp "$temporary/expected" "$temporary/actual"

project=$repository/tests/source/core/basic/project
"$compiler" unstable crown-to-heartwood "$project" >"$temporary/actual"
"$compiler" unstable core print "$project" --input-format source \
    >"$temporary/expected"
cmp "$temporary/expected" "$temporary/actual"

actual_exit=0
"$compiler" --diagnostics jsonl format --check --stdout "$source_file" \
    >"$temporary/stdout" 2>"$temporary/actual" || actual_exit=$?
test "$actual_exit" -eq 2
printf '%s\n' '{"code":"tool.usage","details":{},"message":"argument --stdout: not allowed with argument --check","message_id":"tool.usage","phase":"cli","schema":"sloph.diagnostic","severity":"error","span":{"end":0,"start":0},"version":0}' >"$temporary/expected"
cmp "$temporary/expected" "$temporary/actual"

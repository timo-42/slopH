#!/bin/sh
set -eu

: "${SLOPH_C_COMPILER:?SLOPH_C_COMPILER must name the C bootstrap compiler}"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/sloph-backend.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

"$SLOPH_C_COMPILER" heartwood-to-timber \
  --symbol example::main \
  ../../tests/core/evaluate/parametric-identity/input.core \
  -o "$temporary/unit.c"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic \
  "$temporary/unit.c" -o "$temporary/unit"
"$temporary/unit" > "$temporary/actual"
printf '%s\n' '(value 0 (int 42))' > "$temporary/expected"
cmp "$temporary/expected" "$temporary/actual"

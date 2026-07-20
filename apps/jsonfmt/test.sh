#!/bin/sh
set -eu

formatter=${1:?formatter path is required}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/sloph-jsonfmt.XXXXXX")
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM

printf '{"n":1.00e+2,"x":1,"x":2,"items":[true,null]}' |
  "$formatter" >"$temporary/actual"
cat >"$temporary/expected" <<'EOF'
{
  "n": 1.00e+2,
  "x": 1,
  "x": 2,
  "items": [
    true,
    null
  ]
}
EOF
cmp "$temporary/expected" "$temporary/actual"

if printf '{]' | "$formatter" >"$temporary/bad.out" 2>"$temporary/bad.err"; then
  echo "malformed JSON was accepted" >&2
  exit 1
fi
grep 'sloph-jsonfmt: syntax:' "$temporary/bad.err" >/dev/null

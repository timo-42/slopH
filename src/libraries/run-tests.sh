#!/bin/sh

set -eu

if [ "$#" -eq 0 ]; then
  echo "usage: $0 COMPILER [ARGUMENT ...]" >&2
  exit 2
fi

libraries_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_output=$(mktemp -d "${TMPDIR:-/tmp}/sloph-library-tests.XXXXXX")
trap 'rm -rf -- "$test_output"' EXIT HUP INT TERM

found=false
for project in "$libraries_root"/*/*/tests/*; do
  if [ ! -d "$project" ] || [ ! -f "$project/sloph.json" ]; then
    continue
  fi
  found=true
  layer=$(basename -- "$(dirname -- "$(dirname -- "$(dirname -- "$project")")")")
  library=$(basename -- "$(dirname -- "$(dirname -- "$project")")")
  name=$(basename -- "$project")
  identity="$layer/$library/$name"
  executable="$test_output/$layer-$library-$name"
  actual="$test_output/$layer-$library-$name.stdout"
  errors="$test_output/$layer-$library-$name.stderr"

  "$@" compile "$project" -o "$executable"
  if ! "$executable" >"$actual" 2>"$errors"; then
    cat "$errors" >&2
    echo "$identity: program failed" >&2
    exit 1
  fi
  if [ -s "$errors" ]; then
    cat "$errors" >&2
    echo "$identity: unexpected standard error" >&2
    exit 1
  fi
  if ! cmp "$project/expected.txt" "$actual"; then
    diff -u "$project/expected.txt" "$actual" || true
    echo "$identity: standard output differs" >&2
    exit 1
  fi
  echo "$identity: ok"
done

if [ "$found" = false ]; then
  echo "no library test projects found in $libraries_root" >&2
  exit 1
fi

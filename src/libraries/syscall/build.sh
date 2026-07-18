#!/bin/sh
set -eu

package_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
native_cc=${CC:-cc}
system=$(uname -s)
machine=$(uname -m)

case "$system,$machine" in
  Linux,x86_64|Linux,amd64)
    provider="$package_root/src/posix/linux/amd64"
    output="$provider/libsloph_syscall.so"
    temporary="$output.tmp.$$"
    trap 'rm -f -- "$temporary"' EXIT HUP INT TERM
    "$native_cc" -shared -fPIC -Wl,-soname,libsloph_syscall.so \
      -Wl,--build-id=none -I "$provider" "$provider/syscall.S" -o "$temporary"
    mv -f -- "$temporary" "$output"
    ;;
  Darwin,arm64)
    provider="$package_root/src/posix/darwin/arm64"
    output="$provider/libsloph_syscall.dylib"
    temporary="$output.tmp.$$"
    trap 'rm -f -- "$temporary"' EXIT HUP INT TERM
    "$native_cc" -dynamiclib \
      -Wl,-install_name,@rpath/libsloph_syscall.dylib \
      -I "$provider" "$provider/syscall.S" -o "$temporary"
    mv -f -- "$temporary" "$output"
    ;;
  *)
    echo "unsupported syscall provider build target: $system $machine" >&2
    exit 1
    ;;
esac

trap - EXIT HUP INT TERM

# POSIX boundary

Status: implemented experimental Source/Core v1 boundary.

POSIX descriptor I/O is a normal bundled library, not a mandatory Core
feature. A project opts in with `dependencies = ["syscall"]`, or indirectly
through `dependencies = ["std"]`. The compiler loads the library's SlopH
source and reviewed binding metadata into the same compilation unit as the
application.

## Layers

1. `syscall.h` declares stable compiler-owned C symbols.
2. A target-specific `syscall.c` implements those symbols with one call to the
   POSIX/libc `read` or `write` function. It deliberately does not embed raw
   kernel syscall numbers.
3. `syscall::posix::write_once` exposes one write attempt as `Written`,
   `Interrupted`, or `Error`.
4. `std::io::write` retries interruption and short writes until all bytes have
   been written. A permanent error traps until the language has a standard
   `Result` type.

The currently supported native providers are macOS ARM64 and Linux AMD64.
Their source files are separate even though their implementations are now the
same, so target divergence remains explicit and reviewable.

## Binding metadata and capabilities

`bindings.json` records the C signature, adapter, supported targets, required
capabilities, effects, provenance, and audit facts such as blocking behavior,
allocation, callbacks, pointer access, and pointer retention. Core v1 carries
this metadata in its canonical textual form. The C backend resolves the
foreign name and symbol through that record; POSIX names are not in Core's
built-in primitive table.

These facts are metadata only in v1. They support inspection and later
capability/rule analysis, but the compiler does not yet propagate them through
the call graph or claim that it can prove the NASA/JPL rules.

`read` exists at the C boundary and in metadata, but has no safe SlopH adapter
yet. Exposing it requires a writable borrowed-buffer type with a lifetime that
prevents retention. This is intentionally deferred instead of disguising an
unsafe pointer as immutable `Bytes`.

## Trust boundary

Only compiler-bundled modules may spell `foreign.*` and `runtime.*`
primitives. Ordinary packages call typed library functions. This restriction
is temporary scaffolding for a future reviewed/generated C-header binding
tool, not an assertion that the bundled implementation is Core.

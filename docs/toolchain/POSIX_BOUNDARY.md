# POSIX boundary

Status: implemented experimental Source/Core v1 boundary.

POSIX descriptor I/O is a normal bundled library, not a mandatory Core
feature. A project opts in with `dependencies = ["syscall"]`, or indirectly
through `dependencies = ["std"]`. The compiler loads the library's SlopH
source and reviewed binding metadata into the same compilation unit as the
application.

## Layers

1. Each supported target has a `syscall.h` declaring the same stable,
   compiler-owned C symbols.
2. A target-specific `syscall.S` implements those symbols. The capital `.S`
   suffix selects assembler source with C preprocessing in GCC and Clang.
3. `syscall::posix::write_once` exposes one write attempt as `Written`,
   `Interrupted`, or `Error`.
4. `std::io::write` retries interruption and short writes until all bytes have
   been written. A permanent error traps until the language has a standard
   `Result` type.

The currently supported native providers are macOS ARM64 and Linux AMD64.
Linux AMD64 enters the kernel with the `syscall` instruction and translates
raw negative kernel errors into the public `-1` plus `errno` contract. macOS
ARM64 tail-calls the public libSystem `read` and `write` symbols from assembly;
it deliberately does not depend on private Darwin kernel trap numbers. The
compiler selects both the provider and its matching header from the target
directory.

The complete generated C runtime still depends on libc. This boundary only
makes descriptor I/O target-specific and keeps raw kernel details out of Core
and ordinary packages.

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

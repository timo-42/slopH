# POSIX boundary

Status: implemented experimental Source v1/Core v2-v3 boundary.

POSIX descriptor I/O is a normal bundled library, not a mandatory Core
feature. A project opts in with `dependencies = ["syscall"]`, or indirectly
through `dependencies = ["std"]`. The compiler loads the library's SlopH
source and reviewed binding metadata into the same compilation unit as the
application.

## Layers

1. `syscall::posix` selects one public platform module with a Source v1
   typed `compiler::target::platform` conditional import.
2. `syscall::posix::linux::amd64` and
   `syscall::posix::darwin::arm64` each own their binding metadata,
   `syscall.h`, and a prebuilt shared library.
3. The native adapter returns
   `Result[Int, syscall::posix::native::NativeWriteError]`.
4. `syscall::posix::write_once` maps that into the portable
   `Result[Int, WriteError]` API.
5. `std::io::try_write` retries interruption and short writes, returning
   `NoProgress` or a native error. `std::io::write` remains the compatibility
   wrapper that traps on `Err`.

The currently supported native providers are macOS ARM64 and Linux AMD64.
Linux AMD64 enters the kernel with the `syscall` instruction and translates
raw negative kernel errors into the public `-1` plus `errno` contract. macOS
ARM64 tail-calls the public libSystem `read` and `write` symbols from assembly;
it deliberately does not depend on private Darwin kernel trap numbers. The
resolved SlopH dependency graph carries the provider identity. The backend
reads its matching header and shared-library list from provider metadata and
has no syscall-specific host-platform selector. The compiler never compiles
`syscall.S`; the package-root `build.sh` produces the selected `.so` or
`.dylib` before Source compilation begins.

The complete generated C runtime still depends on libc. This boundary only
makes descriptor I/O target-specific and keeps raw kernel details out of Core
and ordinary packages.

Virtual-memory acquisition uses a separate provider selected by
`syscall::memory`, so descriptor-only programs do not link page operations.
Linux and macOS providers expose reviewed `mmap`/`munmap` adapters. Generated C
stores mapped addresses in a private token registry; Source sees only checked
integer tokens wrapped by the owned `syscall::memory::Page` type. The ordinary
`memory` library implements the initial page-per-allocation `Allocator` and
`Buffer` policy and requires visible `defer memory::drop(buffer);` cleanup.
Allocator policy is not a Core primitive. Foreign provider functions are
package-internal imports, preventing applications from bypassing the checked
`syscall::memory` wrapper with forged tokens.

## Binding metadata and capabilities

Each provider's `bindings.json` records its owning SlopH provider, logical
header, C signature, adapter, required capabilities, effects, provenance, and
audit facts such as blocking behavior, allocation, callbacks, pointer access,
and pointer retention. Core v2 and v3 carry this metadata in their canonical textual
form. The selected SlopH module is the compatibility authority; bindings no
longer carry a handwritten multi-target list.

`provider.json` is bounded inert metadata connecting the provider module to
its binding file and ordered shared-library filenames. Paths are local to the
module-adjacent provider directory. Missing libraries are link-time errors.

The bootstrap automatically executes dependency-package `build.sh` files for
`compile` and `run`. This is temporary arbitrary code execution, not the final
build-task model; its risks and replacement requirements are recorded in
[Secure Package Build Tasks](../../idea/SECURITY.md).

These facts are metadata only in v1. They support inspection and later
capability/rule analysis, but the compiler does not yet propagate them through
the call graph or claim that it can prove the NASA/JPL rules.

`read` exists at the C boundary and in metadata, but has no safe SlopH adapter
yet. Exposing it requires a writable borrowed-buffer type with a lifetime that
prevents retention. This is intentionally deferred instead of disguising an
unsafe pointer as immutable `Bytes`.

## Trust boundary

Raw primitive expressions are not part of Source v1. A provider module may
declare a `foreign fn` only when its identity and complete signature match that
module's reviewed binding metadata. Fixed Core operations are declared only as
`intrinsic fn` members of the bundled `core` package. Ordinary packages call
these typed functions.

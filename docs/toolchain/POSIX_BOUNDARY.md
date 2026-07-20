# POSIX boundary

Status: implemented experimental Source v1/Core v2-v3 boundary.

POSIX descriptor I/O is a normal bundled library, not a mandatory Core
feature. A project opts in with `"dependencies": ["syscall"]`, or indirectly
through `"dependencies": ["std"]`. The compiler loads the library's SlopH
source and reviewed binding metadata into the same compilation unit as the
application.

## Layers

1. `syscall::posix` selects one public platform module with a Source v1
   typed `compiler::target::platform` conditional import.
2. `syscall::posix::linux::amd64` and
   `syscall::posix::darwin::arm64` each own their binding metadata,
   `syscall.h`, and a reviewed assembly source.
3. The native read and write adapters return typed `Result` values. Their
   metadata distinguishes immutable borrowed `Bytes` from a call-scoped
   mutable borrow of an owned `Block`.
4. `syscall::posix::{read_once,write_once}` maps native interruption and error
   values into portable errors.
5. `std::io` retries interruption and short operations and exposes typed
   standard input, standard output, and standard error values.
6. `filesystem` uses reviewed POSIX C providers for path and descriptor
   lifecycle operations, but descriptors never appear in its public API.

The currently supported native providers are macOS ARM64 and Linux AMD64.
Linux AMD64 enters the kernel with the `syscall` instruction and translates
raw negative kernel errors into the public `-1` plus `errno` contract. macOS
ARM64 tail-calls the public libSystem `read` and `write` symbols from assembly;
it deliberately does not depend on private Darwin kernel trap numbers. The
resolved SlopH dependency graph carries the provider identity. The backend
reads its matching header and native-source list from provider metadata and
has no syscall-specific host-platform selector. The compiler passes the
selected `syscall.S` directly to the host C compiler and linker.

The complete generated C runtime still depends on libc. This boundary only
makes descriptor I/O target-specific and keeps raw kernel details out of Core
and ordinary packages.

Explicit byte storage is no longer part of the syscall-provider boundary.
Core defines an opaque owned `Block` capability and privileged allocation,
bounded access, copy, and release primitives. Timber implements those
primitives with the portable C allocator and a separate 256 MiB active-block
budget. Source cannot observe or forge an address. The public `memory` package
re-exports the capability and checked operations, while syscall providers are
limited to services that actually require platform-specific bindings.

## Binding metadata and capabilities

Each provider's `bindings.json` records its owning SlopH provider, logical
header, C signature, adapter, required capabilities, effects, provenance, and
audit facts such as blocking behavior, allocation, callbacks, pointer access,
and pointer retention. Core v2 and v3 carry this metadata in their canonical textual
form. The selected SlopH module is the compatibility authority; bindings no
longer carry a handwritten multi-target list.

`provider.json` format 1 is bounded inert metadata with exactly `format`,
`module`, `bindings`, and `sources` fields. It connects the provider module to
its binding file and a nonempty ordered list of unique `.c` or `.S` inputs.
Paths are local to the module-adjacent provider directory, must be regular
non-symlink files, and are passed directly to the host C compiler. No provider
script is executed and no prebuilt shared library or runtime rpath is needed.

These facts are metadata only in v1. They support inspection and later
capability/rule analysis, but the compiler does not yet propagate them through
the call graph or claim that it can prove the NASA/JPL rules.

The compiler implements a writable borrowed-buffer adapter for `read`. It can
only receive `borrow mut Block`, validates offset and count before entering C,
and the binding contract forbids pointer retention. The ownership checker
therefore keeps the mutable borrow call-scoped.

## Why bindings are typed instead of `syscall_N`

The kernel argument count alone is not a sufficient interface. Two calls with
the same arity can interpret an argument as a signed integer, descriptor,
pointer, byte count, mutable buffer, structure, or pointer retained after the
call. They also differ in blocking, mutation, allocation, and error contracts.
An untyped `syscall_1` through `syscall_6` surface would erase exactly the
information needed for ownership and capability checking, and raw syscall
numbers are not portable between Linux and Darwin.

Binding adapters may still share code according to their lowered C argument
shape. The `posix_checked_call` adapter currently handles reviewed integer,
path, path-plus-integer, and two-path signatures. This gives new native calls
little backend work without making arbitrary kernel entry part of ordinary
Source code.

## C types and pointer capabilities

The bundled `ctypes` package supplies checked fixed-width integer wrappers for
signed and unsigned 8-, 16-, 32-, and 64-bit C values. It also names
integer-shaped 64-bit addresses and length-encoded byte arrays with phantom
`Read`, `Write`, `ReadWrite`, or `Opaque` capabilities. Constructors carrying
the `Unsafe` prefix mark the trust boundary explicitly.

An address is not a normal SlopH `Int`: arithmetic does not grant permission to
dereference it. A provider adapter must establish the capability, lifetime,
mutation, and retention contract. Conversion of a C `(pointer,length)` pair to
owned SlopH `Bytes` must copy the bytes before the foreign lifetime ends;
conversion to a borrowed view is legal only for the dynamic extent of the
foreign call. The current public language cannot store such a borrow.

## Trust boundary

Raw primitive expressions are not part of Source v1. A provider module may
declare a `foreign fn` only when its identity and complete signature match that
module's reviewed binding metadata. Fixed Core operations are declared only as
`intrinsic fn` members of the bundled `core` package. Ordinary packages call
these typed functions.

# IDEA: Foreign Interoperability and Host Boundaries

Status: exploratory, non-normative.

SlopH needs to use maintained native libraries without reimplementing their
functionality. Cryptography is the motivating case: a safe SlopH crypto package
should wrap a maintained C implementation rather than contain a new
implementation of cryptographic algorithms.

Operating-system access has a related but narrower requirement. Trusted
standard-library and runtime modules need a small stable interface for basic
file and process operations without making Linux syscall numbers, macOS
internals, C layouts, or `errno` part of portable SlopH semantics.

This idea explores two C boundaries:

```text
application
    |
    v
safe SlopH standard or crypto library
    |
    v
raw internal C bindings
    |
    +---> external C library
    |
    `---> SlopH Host ABI
              |
              v
        libc / operating system
```

The general C interface is available to libraries that deliberately accept the
foreign-code trust boundary. The Host ABI is a smaller project-owned interface
reserved for trusted standard-library and runtime implementations.

## General C Interoperability

SlopH should support both calling C and exporting C-callable declarations. The
C ABI is the native interoperability contract; it does not require SlopH to use
C as its native code-generation backend.

Conceptual tooling is:

```text
<lang> c import HEADER \
    --target TRIPLE \
    [--sysroot PATH] \
    [-I PATH]... \
    [-D NAME[=VALUE]]... \
    [-o BINDINGS.sloph]

<lang> c export MODULE \
    --target TRIPLE \
    [-o API.h]
```

The exact command and source syntax remain deferred. The semantic operations
are required even if the eventual CLI uses different names.

### Importing C Headers

The import tool should use libclang as an optional toolchain component. It must
not add a C parser, preprocessor, or layout engine to the normal SlopH compiler.
A real C frontend is required because headers can depend on preprocessing,
compiler extensions, target data models, attributes, packing, SDK contents,
and conditional declarations.

The importer receives an explicit target and C compilation environment. It
must not silently read the build host's headers while cross-compiling. Its
semantic inputs include:

- target triple and guaranteed target features;
- deployment target where applicable;
- SDK or sysroot identity;
- C language mode and frontend version;
- include paths and preprocessor definitions;
- the complete transitive header contents;
- importer and generated-binding schema versions.

Generated bindings record these inputs and their hashes as provenance. A
binding module also records an exact target requirement. Importing it for an
incompatible target is a deterministic compilation error rather than an
attempt to reuse a coincidentally similar layout.

The initial supported C subset should include:

- functions with the target C calling convention;
- fixed-width and supported target C scalar types;
- pointers with their C qualifiers;
- fixed-size arrays;
- C-compatible records;
- enums with an explicit foreign representation;
- opaque and incomplete record types;
- function pointers and callbacks;
- evaluable object-like constants with unambiguous values and types.

The importer diagnoses every selected declaration it cannot translate. It must
not silently omit declarations and leave callers believing the binding is
complete. The initial unsupported set includes:

- variadic functions and bitfields;
- C++ and Objective-C declarations;
- compiler-specific layouts not represented by the foreign type profile;
- function-like macros without an explicit signature;
- inline-only functions that have no linkable symbol.

An explicitly selected C shim can turn an unsupported macro, inline function,
or layout into ordinary supported C declarations. Shim sources and all their
inputs are declared target-dependent build-task inputs.

The generated SlopH file contains raw ABI declarations only. A C header does
not contain enough information to soundly infer:

- ownership or destruction responsibility;
- whether a pointer may be null;
- which argument gives a buffer's length;
- aliasing and pointer lifetime rules;
- thread-safety or synchronization requirements;
- which return values denote errors;
- whether secret memory requires clearing.

A library therefore writes an ordinary SlopH wrapper that adds those
guarantees. Automatic safe-wrapper inference is not part of this proposal.

### Exporting C Headers

Only declarations explicitly selected for C export appear in a generated
header. A public SlopH declaration is not implicitly a stable C ABI.

An exported declaration has an explicit C symbol name and uses only types with
a documented C representation. General unbounded `Int`, `Result`, `Option`,
generics, nominal owned values, and ordinary SlopH closures do not cross this
boundary implicitly. A handwritten ABI wrapper expresses such interfaces in
terms of fixed-width values, C-compatible records, opaque handles, pointers,
lengths, status values, and out parameters.

Generated headers use deterministic ordering and formatting, fixed-width C
types where possible, include guards, and C++ `extern "C"` guards. Ownership,
allocation, destruction, nullability, and error conventions remain visible in
the exported API and its documentation. A library returning an owned opaque
handle must also expose its destruction operation.

C-to-SlopH callbacks use an explicitly exported C calling convention. A
synchronous C callback cannot suspend. Async integration requires a
non-suspending trampoline that records or wakes a task through an explicit
protocol.

Header round-tripping means ABI equivalence for the supported subset, not
textual recovery. Preprocessing loses original conditionals, macro spelling,
comments, and typedef style, so:

```text
C header -> SlopH binding -> generated C header
```

preserves supported layouts, symbols, and calls rather than original bytes.

## The Standard-Library Host ABI

The standard library should not directly encode the native signature and
constants of every supported kernel or libc. Instead, the project should own a
small header named conceptually `sloph_host.h`.

Each supported native target supplies an implementation of this header. The
header is imported into a generated SlopH module carrying trusted,
non-forgeable provenance. Only trusted standard-library and runtime modules may
call it directly.

Ordinary packages use safe standard-library APIs. They may use the general C
interface when deliberately binding an external library, but they cannot claim
the trusted Host ABI provenance.

### Shape of the Interface

The Host ABI uses:

- fixed-width integer fields;
- pointer-plus-length byte buffers;
- opaque fixed-width handles that callers cannot inspect;
- capability-relative operations rather than ambient path access;
- normalized operation and error values rather than OS constants;
- explicit result and out parameters rather than ambient `errno`;
- an optional native error code retained for diagnostics.

Exact declarations are deferred, but the interface should resemble:

```c
typedef uint64_t sloph_host_handle;

typedef struct sloph_host_result {
    uint32_t category;
    int32_t native_code;
} sloph_host_result;

sloph_host_result sloph_host_open_at(
    sloph_host_handle directory,
    const uint8_t *path,
    uint64_t path_length,
    uint32_t access,
    uint32_t creation,
    sloph_host_handle *opened);

sloph_host_result sloph_host_read(
    sloph_host_handle file,
    uint8_t *buffer,
    uint64_t capacity,
    uint64_t *read_count);

sloph_host_result sloph_host_write(
    sloph_host_handle file,
    const uint8_t *buffer,
    uint64_t length,
    uint64_t *written_count);
```

This is illustrative rather than final C syntax. The important properties are
fixed representation, explicit lengths and results, and the absence of leaked
OS structures or flag numbers.

Initial portable error categories cover at least invalid arguments, permission
denial, missing resources, existing resources, interruption, would-block,
unsupported operations, resource exhaustion, general I/O failure, and an
unknown native error. The standard library may refine these into typed
operation-specific errors. Native codes are diagnostic information, not
portable control flow.

Unix paths at this boundary are byte sequences. An embedded zero byte is
rejected because an ordinary Unix path API cannot represent it. A higher
standard-library `Path` type owns the user-facing rules. Future targets may use
a different adapter representation without exposing it to portable callers.

### Initial Operation Group

The first synchronous Host ABI group is deliberately small:

- open a file relative to a supplied directory capability;
- read bytes;
- write bytes;
- seek;
- close;
- obtain basic file metadata;
- obtain process arguments;
- obtain startup standard-input, standard-output, and standard-error handles;
- terminate the process with a status.

The startup runtime supplies initial handles and capabilities. A normal native
command may receive broad filesystem authority. A constrained or managed host
may supply narrower directory and stream handles.

Directory operations, sockets, DNS, clocks, secure randomness, environment
access, child processes, memory mapping, event polling, and other services can
be added as separately versioned groups when their standard APIs are designed.
They do not belong in the initial group merely because an OS exposes them as
syscalls.

These calls are synchronous and potentially block their executing thread. The
async model in [ASYNC.md](./ASYNC.md) does not automatically turn a blocking
foreign call into a suspension point. A later event-I/O group can provide the
readiness and wakeup operations needed by a library executor.

### Native Implementations

The macOS implementation should use supported libc, libSystem, and framework C
interfaces. The portable contract must not depend on Darwin syscall numbers.

Linux can compare two implementations behind the same Host ABI:

1. a hosted implementation using libc;
2. a target-specific implementation using documented Linux syscalls when this
   demonstrably improves size, bootstrap simplicity, or performance.

Raw Linux syscalls remain an implementation choice. They do not replace the C
interface needed for crypto and other maintained libraries, and they do not
define portable SlopH semantics.

## Crypto Provider Libraries

Cryptography should have the following package structure:

```text
safe SlopH crypto API
        |
        v
replaceable provider package
        |
        v
generated raw C binding
        |
        v
maintained crypto library
```

Core does not gain cryptographic primitives merely to make binding easier. The
safe package owns algorithm selection, typed keys and nonces, buffer-size
checking, error conversion, secret destruction, and provider initialization.
The provider owns the raw foreign calls.

Provider choice remains replaceable. Libsodium is a useful non-normative
integration proof because it has a C API, supports the initial native systems,
and documents WebAssembly support. Passing its conformance test does not make
libsodium the permanent provider or exclude OpenSSL and other providers.

A package containing native C code is a trusted dependency decision. On a
native target, arbitrary C code can access ambient process state behind the
SlopH wrapper, so the SlopH capability model cannot by itself sandbox that
code. Package manifests and inspection tools expose foreign dependencies and
declared effects rather than hiding them behind generated declarations.

## Target-Dependent Compilation

C headers and libraries make the compilation target a semantic build input.
The following may vary independently:

- header contents and preprocessing branches;
- widths and alignments of C types;
- record layouts and calling conventions;
- available symbols and function signatures;
- library names and formats;
- libc or ABI environment;
- SDK and deployment-target availability;
- guaranteed CPU features.

The target description distinguishes at least:

```text
architecture
vendor
operating system
ABI or environment
pointer width
endianness
C data model
guaranteed CPU features
deployment target where applicable
```

The host and target are separate values. A compiler running on macOS and
targeting Linux selects Linux sources, bindings, sysroot, and libraries. It
must never fall back to an installed macOS header because a Linux input was not
found.

CPU features promised by the target may participate in compile-time selection.
Features detected opportunistically on the eventual execution machine require
runtime dispatch instead. Crypto providers often perform this dispatch.

### How Other Languages Select Targets

Go selects whole files using conventional suffixes:

```text
source_linux.go
source_amd64.go
source_linux_amd64.go
```

It also supports explicit build constraints. File selection makes build-graph
discovery cheap and keeps platform implementations separate, but OS and
architecture suffixes alone do not describe libc, ABI, SDK, or deployment
version differences.

Rust uses configuration attributes with fields such as `target_os`,
`target_arch`, `target_env`, `target_abi`, and `target_feature`. This permits
precise declaration-level selection but can scatter target logic through an
implementation.

Swift provides conditional-compilation blocks using predicates such as
`os(...)`, `arch(...)`, `targetEnvironment(...)`, and `canImport(...)`. Kotlin
Multiplatform combines common and target-specific source sets. .NET uses
runtime identifiers such as `linux-musl-arm64` and `osx-arm64` to select native
package assets. Zig makes CPU, OS, ABI, and OS-version information available to
compile-time code.

### Candidate SlopH Mechanisms

Filename selection could follow a convention such as:

```text
host.linux.sloph
host.macos.sloph
host.linux.x86_64.gnu.sloph
host.macos.aarch64.sloph
```

A declaration-level decorator could express:

```text
@target(os: Linux, arch: X86_64, abi: Gnu)
extern ...
```

A manifest clause could select source sets, bindings, and link inputs:

```text
target x86_64-unknown-linux-gnu {
    sources host_linux.sloph
    c_library ...
}
```

All syntax is illustrative. Three selection levels should be evaluated:

1. filename or source-set selection for complete target implementations;
2. decorators or conditional declarations for small availability differences;
3. manifest target clauses for headers, generated bindings, dependencies, and
   final link inputs.

A hybrid is the leading candidate: files keep platform implementations
separate, decorators handle small declaration differences, and the manifest
owns dependency and linking selection. The exact mechanism, filename order,
predicate grammar, precedence, and overlap rules remain open.

Whichever design is chosen must permit cheap build-graph discovery, report
missing or overlapping variants deterministically, and make every selected
target input part of artifact identity. `--all-targets` checking validates each
advertised target with its own bindings rather than merging incompatible
foreign declarations into one artificial interface.

Whether a portable package must expose an identical public interface for every
advertised target also remains open. Standard portable abstractions should aim
for one interface with target-specific implementations. Explicit platform
packages may need target-only declarations, but that distinction must be
visible in package metadata and generated interfaces.

## Native Linking

Foreign declarations identify types, calling conventions, and symbols; they do
not silently choose a library. Link inputs belong to the explicit target-aware
build graph.

The initial policy for static archives, shared libraries, vendored source,
system libraries, and macOS frameworks remains undecided. Any selected policy
records target conditions and enough resolved identity for reproducible
diagnostics. A header's presence does not imply that an arbitrary library of
the same name is acceptable.

## C Backend and Future Managed Targets

A Core-to-C backend could be useful for bring-up, portability, differential
testing, or bootstrap. It still consumes validated Core and preserves the same
language semantics as a direct native backend. It does not define the foreign
type system: both backends implement the same declared target C ABI.

Whether to build such a backend remains open. Its implementation cost, compile
latency, external C compiler dependency, debug information, and care around C
undefined behavior must be compared with direct native object generation.

Future managed targets cannot assume that a native C ABI or native library is
present. A provider may instead compile its C implementation for the managed
target, expose a portable component, call an explicitly supplied host service,
or use a managed implementation behind the same safe SlopH API. The exact
managed or virtual-machine mechanism remains deferred.

## Validation Direction

Future implementation work should include these shared conformance cases:

- import functions, records, constants, opaque handles, and callbacks from a
  representative C header;
- export a SlopH ABI module and compile its header with Clang and GCC;
- compare C and SlopH size, alignment, field offsets, calls, and symbols;
- diagnose variadics, bitfields, unsupported macros, and missing inline symbols;
- call from C into a non-suspending SlopH callback;
- reject a generated binding under a mismatched target configuration;
- cross-compile conditional headers using only the explicit target SDK;
- diagnose missing and overlapping target variants deterministically;
- exercise Host ABI short I/O, invalid handles, double close, seek and metadata
  errors, path zeros, process arguments, standard streams, and exit status;
- verify ordinary modules cannot forge trusted Host ABI provenance;
- initialize libsodium and run a published known-answer operation through a raw
  binding and safe wrapper;
- compare direct-native and C-backend behavior if both backends exist;
- reject native-only bindings on a managed target without an explicit adapter.

## Open Decisions

- Target-selection syntax, filenames, predicates, precedence, and overlap.
- Whether portable packages preserve one public interface across targets.
- Exact foreign scalar, pointer, record, enum, and callback syntax.
- Exact Host ABI declarations, errors, versioning, and extension negotiation.
- Static, shared, framework, vendored, and system native-link policy.
- C shim declaration and build workflow.
- Whether and when a Core-to-C backend is implemented.
- Async event and readiness Host ABI operations.
- Future managed or virtual-machine interop.

## References

- [POSIX.1-2024 introduction and C system-interface scope][posix]
- [Linux userspace ABI stability documentation][linux-abi]
- [libclang tutorial and API-stability guidance][libclang]
- [Go build constraints and filename selection][go-build]
- [Rust conditional compilation reference][rust-cfg]
- [Swift compiler control statements][swift-conditional]
- [Kotlin Multiplatform source sets][kotlin-source-sets]
- [.NET runtime identifier catalog][dotnet-rid]
- [Zig target model example][zig-target]
- [WebAssembly Component Model WIT reference][wit]
- [Libsodium installation and platform documentation][libsodium]

[posix]: https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/V1_chap01.html
[linux-abi]: https://docs.kernel.org/admin-guide/abi.html
[libclang]: https://clang.llvm.org/docs/LibClang.html
[go-build]: https://go.dev/cmd/go/
[rust-cfg]: https://doc.rust-lang.org/reference/conditional-compilation.html
[swift-conditional]: https://docs.swift.org/swift-book/ReferenceManual/Statements.html
[kotlin-source-sets]: https://kotlinlang.org/docs/multiplatform/multiplatform-discover-project.html
[dotnet-rid]: https://learn.microsoft.com/dotnet/core/rid-catalog
[zig-target]: https://ziglang.org/download/0.6.0/release-notes.html
[wit]: https://component-model.bytecodealliance.org/design/wit.html
[libsodium]: https://libsodium.gitbook.io/doc/installation

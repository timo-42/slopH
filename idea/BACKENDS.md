# IDEA: Native and WebAssembly Backends with Inferred Compatibility

Status: exploratory, non-normative.

SlopH should have two production code-generation backends:

```text
validated typed Core
    |
    +-- native backend -> machine code and native objects/executables
    |
    `-- WebAssembly backend -> Wasm module + generated JavaScript bindings
```

The native backend targets operating-system object and executable formats. The
WebAssembly backend targets Node, Bun, and browsers through generated,
versioned JavaScript helpers. The existing experimental Core-to-C11 path remains
a bring-up and differential-testing bridge; it is not a third long-term
production backend.

Library authors should not maintain a redundant list of compatible backends and
hosts. Compatibility is inferred transitively from a small set of explicit host
and foreign boundaries. Authors declare facts only where opaque C, JavaScript,
assembly, or backend-specific code prevents the compiler from deriving them.
The registry independently builds and verifies the inferred matrix.

## Goals

- Preserve one language and Core semantics across native and Wasm output.
- Generate deterministic JavaScript and TypeScript bindings with Wasm modules.
- Infer backend and host requirements through ordinary call and interface
  summaries.
- Keep portable declarations usable even when the same package contains
  platform-specific declarations.
- Let safe APIs select native, Wasm, or JavaScript providers without changing
  callers.
- Diagnose the exact boundary that makes a reachable program incompatible.
- Publish inferred and independently verified compatibility in registry
  metadata.
- Keep source packages canonical when optional native, Wasm, or JavaScript
  artifacts are unavailable.

## Non-Goals

- A handwritten `supports = [...]` list as the authority for compatibility.
- Treating Node, Bun, and browsers as identical operating environments.
- Making all SlopH libraries target the lowest common denominator of Wasm.
- Assuming a C library is available in Wasm because its API performs no
  syscalls.
- Treating a successful compile as evidence that runtime behavior was tested.
- Requiring generated JavaScript for native applications.
- Defining JavaScript object behavior as the language's canonical semantics.

## Backend, Target, and Host Are Separate Axes

The toolchain distinguishes:

```text
backend       native | wasm
target        architecture, operating system, ABI, and feature set
host profile  native | browser | node | bun
```

Examples include:

```text
backend native, target aarch64-macos, host native
backend native, target x86_64-linux, host native
backend wasm,   target wasm32,        host browser
backend wasm,   target wasm32,        host node
backend wasm,   target wasm32,        host bun
```

The tuple is part of artifact and cache identity. `wasm32` alone describes the
code representation but not the available filesystem, networking, process,
clock, randomness, module-loading, or UI services.

The initial Wasm backend should use ordinary linear-memory Wasm and a
documented SlopH-to-JavaScript ABI. WASI and the WebAssembly Component Model may
later provide additional host profiles or component packaging; they are not
required to run the initial module in JavaScript hosts.

## Native Backend

The native backend consumes validated Core and directly emits machine code,
object files, and eventually executables for supported targets. Initial target
families are:

```text
Mach-O ARM64 on Apple Silicon macOS
ELF x86-64 on AMD64 Linux
```

Additional architectures and object formats require explicit target support.
Native C ABI calls, system linkers, runtime objects, and target-specific Host
ABI providers remain declared build-graph inputs.

The direct backend and the experimental C11 bridge must agree on observable
language behavior for the Core profiles both support. The C11 bridge does not
define the final native ABI, optimization strategy, or backend feature set.

## WebAssembly Backend and Generated Bindings

A Wasm build conceptually produces:

```text
example.wasm
example.mjs
example.d.ts
example.browser.mjs
example.node.mjs
example.bun.mjs
example.wasm.map
```

Only required files are emitted. `example.mjs` contains environment-neutral
ABI machinery. Thin host adapters are generated when loading or capability
provision differs. A package using only the shared JavaScript/Wasm subset may
need one adapter usable by several hosts; Bun-specific output is not generated
merely because Bun is a named host profile.

The generated binding layer handles admitted forms of:

- module instantiation and versioned import tables;
- exported functions and values;
- integers, floats, booleans, strings, byte arrays, slices, structs, enums,
  `Option`, and `Result`;
- linear-memory allocation, ownership transfer, and resource destruction;
- memory-view refresh after memory growth;
- opaque handles for resources that cannot cross the ABI by value;
- callbacks through validated handle tables;
- error and trap translation;
- suspending exports as JavaScript promises;
- source maps and stable diagnostic identities.

The exact admitted ABI surface begins small. Unsupported exported types fail
with a declaration-specific diagnostic rather than receiving an unstable ad
hoc encoding.

JavaScript and TypeScript files are generated from one canonical validated ABI
description. The ABI schema, binding-generator version, integer mapping,
ownership rules, exception behavior, callback lifetime, and promise behavior
enter artifact compatibility identity.

Generated JavaScript contains no ambient package-manager lookup, undeclared
network access, or target detection that changes semantics. Host selection is
explicit at build or instantiation time.

## Compatibility Originates at Boundaries

Ordinary SlopH functions do not declare target compatibility. Requirements
originate only at operations whose implementation or authority crosses the
portable language boundary:

```text
SlopH code
    |
    +-- project Host ABI operation
    +-- imported C declaration or native library
    +-- imported JavaScript host function
    +-- inline assembly or architecture intrinsic
    +-- backend-specific Core/runtime primitive
    `-- selected target-specific provider
```

This is broader than a syscall boundary. Browser `fetch` is a JavaScript host
import, not a syscall visible to the Wasm module. A computational C crypto
library might make no syscalls but still needs a native library or a separately
compiled Wasm provider.

Toolchain-owned boundary declarations carry validated requirement summaries:

```text
HostFileSystem.open
    requires { host.filesystem }

BrowserFetch.send
    requires { web.fetch, javascript.imports }

Process.spawn
    requires { host.process }

NativeCrypto.verify
    requires { backend.native, foreign.c }

WasmCrypto.verify
    requires { backend.wasm, wasm.linear-memory }
```

Names are provisional. The requirement catalog is a small, versioned set, not
arbitrary strings whose meaning each package invents.

## C and JavaScript Boundary Declarations

The compiler cannot derive hidden behavior from a C symbol signature or a
JavaScript import name. Foreign declarations therefore supply explicit
requirements and effects:

```text
extern c fn randombytes_buf(output: mut Slice[Byte])
    requires { backend.native, foreign.c, host.secure-random }
    effects { unsafe, nondeterministic }

extern c fn crypto_verify(expected: Slice[Byte], actual: Slice[Byte]) Int
    requires { backend.native, foreign.c }
    effects { unsafe }

extern javascript fn fetch(request: JsRequest) JsPromise
    requires { backend.wasm, web.fetch }
    effects { io, may_suspend }
```

A provider may offer the same safe interface through different boundaries:

```text
CryptoProvider
    +-- libsodium-native  requires { backend.native, foreign.c }
    +-- libsodium-wasm    requires { backend.wasm }
    `-- web-crypto        requires { backend.wasm, web.crypto }
```

Provider resolution selects one compatible implementation and records it in
the build graph. The safe consumer remains backend-neutral.

An opaque foreign declaration without validated metadata receives a
conservative summary including unknown host access, allocation, blocking,
nondeterminism, traps, callbacks, and unsafe behavior. Restricted targets and
audit profiles reject it until a trusted wrapper supplies a narrower contract.

Foreign metadata is a claim at ordinary package boundaries and may require
registry audit or explicitly trusted provenance. Toolchain-owned Host ABI and
JavaScript adapter summaries receive stronger compiler provenance.

## Transitive Inference

Requirements propagate through the same bounded mechanism as function effects:

```text
fn read_config(path: Path) Config {
    bytes = FileSystem.read(path)
    return parse(bytes)
}
```

Conceptually produces:

```text
parse       requires {}
read_config requires { host.filesystem }
```

Compiled interfaces record requirement summaries for exported declarations,
higher-order callbacks, generic constraints, and selected providers. A
consumer does not need dependency bodies merely to infer compatibility.

The application algorithm is:

1. select reachable declarations;
2. resolve generic applications and interface witnesses;
3. select target-specific providers;
4. union transitive host and backend requirements;
5. compare them with the backend, target, and host-profile capabilities;
6. report unsatisfied requirements with deterministic call and provider paths;
7. generate code and the required binding artifacts.

This integrates with reachability-driven compilation. A package can contain a
portable parser and a native-only file loader. A browser application reaching
only the parser remains compatible.

## Declaration, Feature, and Package Compatibility

Compatibility is fundamentally per exported declaration under a selected
feature set:

```text
parse       requires {}
encode      requires {}
load_file   requires { host.filesystem }
watch_file  requires { host.filesystem.watch }
```

Registry presentation may summarize these groups, but a single package-level
`wasm-compatible` Boolean loses necessary information.

For publication, complete-package checking evaluates every exported
declaration for the package's supported feature combinations and records
conditions. For an application, only selected features and reachable
declarations matter.

Authors may optionally declare a support commitment such as hosts they test and
intend to maintain. That statement is publisher presentation or policy
metadata, not the compiler's compatibility authority. Compiler inference may
discover additional compatible hosts or contradict a publisher claim.

## Host Profiles

Each host profile publishes a validated set of services and backend features.
Conceptually:

```text
browser-baseline provides {
    wasm.linear-memory
    javascript.es-modules
    web.fetch
    web.crypto
    host.clock.monotonic
}

node provides {
    wasm.linear-memory
    javascript.es-modules
    host.filesystem
    host.network
    host.process
    host.clock
}

bun provides {
    wasm.linear-memory
    javascript.es-modules
    host.filesystem
    host.network
    host.process
    host.clock
}
```

These examples do not assert that every version of a host implements an
identical API. Real profiles identify host and profile versions. Optional Wasm
SIMD, threads, shared memory, exception handling, garbage collection, memory64,
and other proposals are separate feature requirements rather than assumptions
of baseline Wasm.

Compatibility is set satisfaction after provider resolution:

```text
reachable requirements subset-of selected host capabilities
```

Some requirements carry structured versions, limits, or mutually exclusive
choices and need more than string-set inclusion. The compiler uses a bounded
typed constraint solver, not arbitrary package code.

## Registry Inference and Verification

Registry metadata keeps three sources distinct:

```text
publisher commitment
compiler-inferred requirements
registry-verified build and test results
```

An example report is:

```text
publisher commitment:
    browser, node

inferred:
    portable exports: parse, encode
    host.filesystem: load_file
    web.fetch: fetch_document

verified:
    native/aarch64-macos: pass
    native/x86_64-linux: pass
    wasm32/browser: pass for portable and web features
    wasm32/node: pass
    wasm32/bun: not tested
```

After upload, the registry:

1. independently validates foreign and provider metadata;
2. infers per-export and per-feature requirements;
3. builds declared registry test profiles for native and Wasm targets;
4. runs Wasm artifacts under the applicable JavaScript hosts where available;
5. validates generated JavaScript, TypeScript, import tables, and ABI schemas;
6. publishes signed compatibility attestations bound to source, dependency,
   compiler, backend, host-profile, and test identities.

`not tested`, `compile pass`, and `runtime tests pass` remain distinct states.
AI-generated registry summaries may describe compatibility but are never
evidence for it.

## Registry Artifacts

Canonical source remains mandatory. Optional verified artifacts include:

```text
native-object
native-executable
wasm-module
javascript-binding
javascript-host-adapter
typescript-declaration
wasm-source-map
```

Each download record identifies backend, target, host profile, ABI schema,
features, compiler, optimization mode, exact hash, and size as applicable. A
Node adapter is not silently reused in a browser because both load the same
`.wasm` bytes.

## CLI Direction

Conceptually:

```text
sloph compile --backend native --target aarch64-macos

sloph compile --backend wasm --target wasm32 --host browser
    --emit wasm,javascript,typescript

sloph check --backend wasm --host node

sloph package info NAME --compatibility
```

Invalid tuples fail early. Browser artifacts may be compiled and tested but
not directly launched by an ordinary terminal `run` command without an
explicit browser harness. Node and Bun host profiles may provide CLI runners.

## Validation and Performance

Conformance tests include:

- identical pure Core behavior under native and Wasm backends;
- deterministic Wasm, JavaScript, TypeScript, and import-table output;
- strings, bytes, enums, results, errors, callbacks, resource destruction, and
  suspending exports through the JavaScript ABI;
- browser, Node, and Bun adapter loading where supported by CI;
- a package containing portable and native-only exports whose browser build
  reaches only portable code;
- native C, Wasm-compiled C, and JavaScript crypto providers behind one safe
  interface;
- rejection of an unannotated or incompatible C/JavaScript boundary;
- exact call paths for filesystem, process, thread, SIMD, or other unsatisfied
  requirements;
- registry inference disagreement with publisher commitments;
- source-only rebuilding when every optional backend artifact is absent.

Measurements include backend compile latency, peak compiler memory, Wasm and
binding sizes, native and Wasm runtime performance, startup and instantiation
latency, JavaScript crossing cost, memory growth, callback and promise overhead,
cache reuse, and artifact download size.

## Questions to Resolve

- Initial SlopH-to-JavaScript ABI schema and admitted exported types.
- Whether one neutral loader plus adapters or fully separate host bindings
  produce the smallest stable surface.
- Wasm memory allocation, ownership, resource handles, and cleanup protocol.
- Exception, trap, cancellation, and promise mapping.
- Callback lifetime and reentrancy rules.
- Host-profile names, versioning, capability catalog, and provider precedence.
- How registry CI executes browser tests reproducibly and within limits.
- Whether optional Wasm features select separate modules or one negotiated
  artifact.
- Future relationship to WASI and the Component Model.
- Native dynamic libraries and Wasm component linking.
- Which foreign metadata receives trusted versus publisher-claim provenance.

## Related Ideas

- [Foreign Interoperability and Host Boundaries](./INTEROP.md)
- [Caller-Controlled Async Functions](./ASYNC.md)
- [Purity and a Small Function Effect System](./EFFECTS.md)
- [Reachability-Driven Compilation](./REACHABILITY_COMPILATION.md)
- [Library Registry Metadata and GitHub-Backed CDN](./LIBRARY_CDN.md)
- [Audit Commands and Requirement Profiles](./AUDIT_PROFILES.md)

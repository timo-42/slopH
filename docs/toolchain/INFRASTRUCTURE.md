# Compiler and Package Infrastructure Requirements

This document defines requirements for the compiler, build system, package
registry, and compiled artifacts. It is subordinate to the top-level goals in
[REQUIREMENTS.md](../../REQUIREMENTS.md). An infrastructure feature may be rejected
when it adds more operational or conceptual cost than the latency it removes.

The details below are requirements and design constraints, not a promise that
every mechanism must exist in the first prototype.

The proposed single-binary compiler, inspection, package-client, and future
registry-server command interface is specified in [CLI.md](./CLI.md).
The future auditable source-to-self-hosting path is specified in
[BOOTSTRAP.md](./BOOTSTRAP.md); it is not a prerequisite for the first compiler
implementation.
The implementation-neutral language, Core, desugaring, backend, CLI, and
bootstrap test architecture is specified in [TESTING.md](./TESTING.md).

## Primary Outcome

A large project must remain fast to check, build, test, and run from source,
including with an empty cache. The toolchain must not make remote builders,
shared caches, binary registry artifacts, or elaborate environment managers
necessary for an acceptable development loop.

The reference workload is a project containing approximately 200,000 lines of
project code and 150 library dependencies. It must exercise realistic modules,
generics, macros, and generated code rather than consist of trivial repeated
files.

Before v1, the project must define a reference machine and quantitative latency
and peak-memory budgets for this workload. Measurements must include:

- dependency download and project bootstrap;
- a clean check and clean native build from canonical source;
- a no-change build;
- a one-module implementation change;
- a public-interface change deep in the dependency graph;
- test discovery, compilation, linking, and startup;
- several clean builds in independent worktrees running concurrently.

Compiler phase timings, critical dependency paths, cache hits, cache misses,
and peak memory must be inspectable. A performance claim without the workload,
machine, cache state, and compiler version is incomplete.

## Build Graph

The compiler must represent the build as an explicit dependency graph rather
than repeatedly discovering dependencies during compilation.

The graph distinguishes at least:

- package and module dependencies;
- public-interface dependencies;
- implementation-only dependencies;
- compile-time syntax and macro dependencies;
- build-task inputs and generated outputs;
- target and feature dependencies;
- final native linking dependencies.

A cheap initial scan may read module headers, imports, declared syntax imports,
and build-task declarations. It must not require full type checking merely to
discover the build graph.

Independent graph nodes should compile in parallel in one compiler invocation
using a bounded worker pool. Scheduling must account for memory as well as CPU;
starting every ready task at once is not acceptable when that causes excessive
peak memory or makes concurrent worktrees unusable.

One project build should normally use one long-lived compiler process or
coordinated worker pool so parsing tables, standard interfaces, and common
compiler state can be shared safely. Correctness must not depend on a daemon.

## Separate Compilation and Module Interfaces

Each compiled module exposes a compact, deterministic, versioned interface.
Importers should normally need the interface, not the dependency's source AST
or implementation body.

An interface contains only information required by consumers, including as
applicable:

- stable package, module, declaration, type, and constructor identities;
- exported names and visibility;
- fully checked public types and signatures;
- nominal type representation and ABI information that is intentionally
  public;
- typeclass, instance, witness, ownership, and effect information required by
  callers;
- exported syntax, operator, and macro declarations;
- validated macro code and the metadata needed to invoke it;
- hashes of semantically relevant public information;
- compiler, schema, target, feature, and standard-profile compatibility data.

Interface serialization must be deterministic and quick to validate and load.
Private declarations, source locations not needed by consumers, and optimizer
implementation details must not invalidate an interface hash.

Changing a module body without changing its semantic interface must not force
dependent modules to be type-checked again. Changing an interface invalidates
only its transitive consumers and the artifacts whose results can actually
change.

The language design must preserve module-local checking wherever practical.
Public declarations may require explicit signatures when doing so prevents
global inference or unstable interfaces. Features requiring whole-program name
resolution, type inference, instance search, ownership solving, or lifetime
solving are presumed unsuitable unless they demonstrate bounded separate
compilation.

Dependency cycles must either be rejected or handled by an explicit mechanism
whose interfaces and scheduling rules remain deterministic. The compiler must
not rely on heuristic repeated checking until a cycle happens to converge.

## Compilation Work

### Library-First Toolchain

The command-line executable must be a thin frontend over reusable toolchain
libraries. Parsing, formatting, expansion, checking, Core inspection,
compilation, interface handling, package resolution, and artifact validation
must remain callable without invoking a subprocess or emulating CLI arguments.

Public libraries use typed inputs, results, options, and diagnostics. External
effects are supplied explicitly rather than obtained from ambient process state.
The libraries do not terminate their host process or write directly to global
standard streams. This permits the compiler CLI, language server, tests, build
services, and user-authored tools to share one implementation.

Once supported by the language and registry, stable toolchain libraries should
be published as independently usable packages. Components for Syntax and Core
inspection must not require linking the native backend or the complete package
manager. Compiler-private passes and lower IR remain internal APIs without a
compatibility guarantee. The complete proposed separation is recorded in
[CLI.md](./CLI.md#library-first-implementation).

Parsing must be linear in input size and avoid unnecessary allocation. Parsed
trees and intermediate representations should be released after their last
consumer instead of retaining the entire project in memory.

Macro expansion, type checking, Core validation, and optimization must have
bounded and observable work. The compiler must diagnose exceeded limits rather
than hang or silently consume unbounded resources.

Generic code must support separate compilation. Dictionary or witness passing
is the default conceptual fallback; mandatory transitive monomorphization is
not assumed. Selective specialization is permitted when bounded, profitable,
and cacheable.

The default build pipeline uses a small number of deterministic Core passes.
Cross-module inlining, whole-program specialization, and link-time optimization
must be optional optimization modes, not prerequisites for normal builds or
acceptable runtime behavior.

The initial native backend and linker path must favor predictable low latency.
A slower optimizing backend may be offered separately. Both paths consume the
same validated typed Core and must preserve language semantics.

## Future Managed Execution Targets

Native executables remain the required v1 output. A later compiler may also
lower validated typed Core to one or more established managed execution
formats. The current preferred first managed target is the
[WebAssembly Component Model](https://component-model.bytecodealliance.org/)
with WASI host interfaces.

The purpose of a managed target is portable, sandboxed hosting with fast cold
starts and high application density. The density benefit must come from a host
runtime that safely runs many isolated applications and shares runtime
infrastructure between them. Bytecode by itself is not assumed to use less CPU
or memory than native code.

Every managed backend must:

- preserve the same observable language semantics as the native backend;
- compile independently from validated typed Core rather than through another
  surface language;
- emit deterministic, versioned artifacts that can be validated with bounded
  time and memory before execution;
- expose platform services through explicit capabilities rather than ambient
  filesystem, network, clock, randomness, environment, or secret access;
- support per-application memory, CPU, execution, and I/O limits;
- allow a host to interrupt or terminate an application that exceeds its
  limits without corrupting other applications;
- isolate application memory and mutable runtime state while permitting safe
  sharing of immutable code and runtime infrastructure;
- report unsupported target capabilities at compile or deployment time.

Managed targets do not define the language's semantics, and SlopH must not
become the lowest common denominator of every supported VM. Target-specific
services remain explicit optional capabilities. JVM bytecode, .NET IL, or
JavaScript compatibility output may be added later when concrete hosting demand
justifies their specification, implementation, and testing costs.

Designing a custom SlopH VM is a non-goal. It may be reconsidered only if
measurements show that established runtimes cannot meet explicit requirements
for artifact size, validation and startup latency, steady-state memory,
application density, sandboxing, or host integration.

## Registry Package Contents

Canonical compressed source and a complete manifest are the portable source of
truth for a registry package. A conforming compiler must be able to build a
package when every optional binary artifact is absent or incompatible.

A registry may additionally distribute the following accelerators:

1. **Compiled module interfaces.** Small, quick-to-load public contracts used
   for dependency checking and build-graph construction.
2. **Validated compile-time code.** Target-independent macro code in a bounded,
   pure representation, accompanied by its declared inputs and limits.
3. **Validated typed Core.** A target-independent artifact that avoids parsing,
   macro expansion, and type checking while retaining enough structure for the
   local compiler to validate and lower it.
4. **Native objects.** Optional target-, ABI-, feature-, and optimization-
   specific artifacts for the fastest compatible link path.

A binary source AST may be used as a disposable local cache, but it is not the
preferred registry contract. Parsing is only one compiler phase, an AST is
tightly coupled to parser and syntax versions, and an AST still requires macro
expansion and type checking. Interfaces and typed Core avoid more verified work
and therefore justify a stable format more readily.

Registry artifacts must not silently replace language semantics. The package's
source, manifest, lock data, declared build inputs, and compiler rules determine
meaning; binary artifacts are validated consequences of those inputs.

## Artifact Identity and Compatibility

Every reusable artifact has a content-derived identity covering all inputs
that may affect its meaning or generated code. Depending on the artifact, this
includes:

- canonical source and manifest hashes;
- exact dependency interface or artifact hashes;
- compiler semantic version and artifact-schema version;
- Core and primitive-catalog version;
- standard-library profile and ABI version;
- target triple, CPU features, calling convention, and optimization mode;
- enabled language and package features;
- macro code and declared compile-time inputs;
- build-task outputs and their declared input hashes;
- relevant compiler options.

Compatibility must be established by explicit version and capability checks,
not by attempting to decode an artifact and hoping it works. An incompatible
or corrupt accelerator is a cache miss: the compiler falls back to a compatible
lower-level artifact or canonical source and reports a concise diagnostic when
the condition warrants user attention.

Artifact schemas must be small, documented, length-delimited, and safe to skip
or reject. Decoders enforce size, recursion-depth, collection-length, and
resource limits before allocation. Unknown executable or semantic fields may
not be ignored unless the schema explicitly defines that behavior as safe.

## Trust, Validation, and Reproducibility

Registry packages and all derived artifacts are untrusted input. Loading a
binary artifact must never grant filesystem, network, process, environment, or
native-code execution authority.

The compiler validates at the appropriate boundary:

- package hashes and signatures establish provenance and integrity;
- interfaces are checked for schema, identity, dependency, and type-system
  consistency;
- macro artifacts are validated against the bounded compile-time machine and
  declared capabilities;
- typed Core is structurally validated, type checked, and checked against the
  primitive catalog before optimization or lowering;
- native objects are accepted only for an exact compatible target and trusted
  provenance policy.

Signatures prove who published bytes; they do not prove that code is safe or
correct. The registry and package manager must expose publisher identity,
checksums, dependency identity, and artifact provenance for auditing.

Given the same canonical inputs and declared toolchain, compilation must be
reproducible. Timestamps, absolute workspace paths, directory enumeration
order, worker scheduling, and undeclared environment state must not change
semantic interfaces or generated artifacts. Debug information that embeds
local paths must use an explicit reproducible mapping.

Where practical, the ecosystem should support independently rebuilding
registry accelerators from source and comparing their identities. A publisher-
supplied artifact may be rejected by policy without making its source package
unusable.

## Local Cache

The compiler may maintain a content-addressed local cache for interfaces,
expanded modules, checked Core, optimized Core, and native objects. Cache
entries are disposable: deleting the cache must affect performance only, not
correctness or available language behavior.

Cache lookup and validation must be cheaper than recomputing the result. Cache
metadata must remain bounded and support deterministic garbage collection by
size, age, or reachability from active projects and lockfiles.

Concurrent compiler processes and independent worktrees must not corrupt cache
entries or require a single global lock. Artifacts should be written
atomically, and identical concurrent work should converge on the same content
identity.

Remote caches are optional transports for the same validated artifacts. Builds
must remain correct when a remote cache is unavailable, slow, malicious, or
returns a miss.

## Build Tasks

External build tasks follow the capability and declaration rules in
[MACRO.md](../language/MACRO.md). They are exceptional graph nodes, not an unrestricted
escape from deterministic compilation.

A task declares its inputs, outputs, target dependence, environment access,
network access, and process capabilities. Undeclared state cannot participate
in a cacheable or reproducible result. Generated outputs live in the build
area and cannot silently modify tracked source or dependency packages.

The build system must show why a task ran, which input invalidated it, and
whether its result came from a local or remote artifact. Network access is off
by default during ordinary compilation and must never be hidden inside syntax
expansion or type checking.

## Diagnostics and Inspection

Infrastructure behavior must be understandable by humans and AI tools. The
compiler and package manager must provide machine-readable and concise human-
readable views for:

- the resolved package and module graph;
- why a module or artifact was rebuilt;
- interface changes between two builds;
- cache and registry artifact selection or rejection;
- per-phase time, memory, and generated-code size;
- macro expansion and build-task dependency chains;
- the exact compiler, target, feature, and standard-profile configuration.

Diagnostics must be deterministic. Parallel execution may not reorder errors
nondeterministically; a stable source and dependency ordering decides how they
are presented.

## Required Performance Tests

The proposed runner, result schema, environment fingerprint, baseline policy,
and concrete compiler workload families are described in the exploratory
[benchmarking design](../../idea/BENCHMARKING.md). Regardless of the eventual
runner interface, the requirements in this section are mandatory.

The toolchain repository must continuously test at least:

- a representative 200,000-line, 150-library reference project;
- cold source-only builds with all optional artifacts disabled;
- builds using interfaces and typed-Core registry artifacts;
- warm no-change and local-change builds;
- deep public-interface invalidation;
- macro-heavy but valid code at declared limits;
- concurrent independent worktree builds;
- cache corruption, cache deletion, and remote-cache failure;
- incompatible compiler, schema, standard-library, target, and feature data;
- deterministic rebuilds with different paths and worker counts;
- malicious or malformed package artifacts under decoder resource limits.

Once a managed target exists, continuous tests must additionally measure its
artifact size, validation and cold-start latency, idle and active per-instance
memory, throughput, quota enforcement, cross-application isolation, and
applications per host. Results must be compared with the native backend and
reported without assuming that managed execution is inherently cheaper.

Regressions are evaluated against complete edit/check/test/run latency and peak
resource use, not compiler throughput alone. Improvements that depend on a warm
cache must be reported separately from source-only improvements.

## Deferred Decisions

The following choices remain open until measurements justify them:

- concrete latency and memory budgets and the reference hardware;
- module-interface and typed-Core serialization formats;
- whether the compiler uses processes, threads, or both for workers;
- the initial native backend and linker implementation;
- registry signing, transparency, and publisher-trust mechanisms;
- additional managed targets and the compatibility lifetime of binary
  artifacts;
- remote-cache protocol and whether a public registry hosts native objects;
- package resolution, lockfile, vendoring, and offline-workflow details.

These decisions may change implementation strategy, but they may not weaken
the source fallback, validation, determinism, inspectability, or clean-build
requirements above.

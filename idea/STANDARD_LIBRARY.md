# IDEA: Layered Standard Library and Package Bundle

Status: exploratory, non-normative.

SlopH should ship a standard library, but the complete batteries-included
experience should not become one compiler-coupled compatibility surface. The
distribution should combine a small mandatory library with independently
versioned official packages and a tested bundle of those packages.

This aims to provide Python-like availability of useful functionality while
retaining the Haskell-style ability to evolve, replace, and version libraries
independently.

## Proposed Layers

The distribution has three conceptual layers:

| Layer | Compatibility | Purpose |
| --- | --- | --- |
| Language and Core | Extremely stable | Syntax, types, evaluation, effects, primitives, and ABI-relevant semantics |
| Mandatory `core` library | Compiler-coupled | Types and protocols whose identity or behavior is required by the language |
| Official package bundle | Independently versioned | General-purpose libraries selected and tested together |

The exact package names are deferred. A possible organization is:

```text
sloph-core       mandatory and compiler-coupled
sloph-std        conservative portable foundations
sloph-batteries  manifest selecting tested official package versions
```

The package bundle is a version set rather than one inseparable library. A
project may depend on the bundle for convenience or select individual packages
when it needs a smaller dependency graph or different implementations.

Compiler-shipped libraries should use the same package metadata and resolution
rules as other libraries. The compiler installation contributes the final root
to an ordered package search path, allowing project or CLI roots to provide
compatible replacements for ordinary standard packages while protecting the
compiler-coupled `core` identity. See
[Package Metadata and Ordered Search Paths](./PACKAGE_SEARCH_PATH.md).

## Mandatory Core Library

The mandatory library should contain an abstraction only when changing or
replacing it would effectively change the language. Likely contents include:

- `Bool`, `Unit`, `Option`, `Result`, and ordering types;
- conventional numeric types built around the language's numeric semantics;
- the primitive text or string representation, if one is language-defined;
- fundamental equality, comparison, hashing, and iteration protocols;
- memory-management interfaces required by the selected language model;
- compiler-known transformations and trusted assembly interfaces;
- minimal allocation, panic, trap, and runtime-boundary operations.

This layer must remain small enough that its complete relevant semantics fit
comfortably within the project's AI context budget. Filesystem access,
networking, serialization, executors, and other replaceable facilities do not
belong here merely because most programs need them.

Implicit imports, if any, should expose only a very small prelude and should be
possible to disable. A package's dependency on other library functionality
must remain explicit in its manifest and compiled interfaces.

## Official Foundations and Batteries

The compiler distribution should include, or make immediately available
offline, a tested set of official packages for ordinary application work.
Candidates include:

```text
collections  text       formatting  parsing
io           filesystem process     time
random       math       async        synchronization
networking   json       http         cli
observability testing
```

The official testing packages should provide the bundled framework, typed
fixtures, protocol interceptors, deterministic simulation, and failure
injection described in
[Bundled Testing, Interceptors, and Fault Injection](./TEST_FRAMEWORK.md). They
remain outside mandatory runtime `core` and enter only selected test targets.

Observability should likewise be delivered as independently versioned official
packages for traces, metrics, structured logs, transformations, OpenTelemetry,
OTLP, Prometheus, and testing, with profiling and flame-graph support added as
a later independent package. A code-free virtual package should select a tested
compatible set for applications that want the complete experience. The proposed
boundaries and semantic instrumentation model are described in
[Official Observability Packages and Virtual Bundle](./libraries/observability.md).
Unused signal packages, exporters, encoders, and transformations must remain
outside the selected build even when the virtual bundle is present.

Inclusion in the distribution does not imply implicit availability to every
program. Unused packages must not be parsed, typechecked, compiled, linked, or
downloaded as part of a build. This is necessary to preserve fast clean builds
and cheap isolated agent worktrees.

Official packages may have different stability grades. A small portable
foundation can make a stronger compatibility promise, while rapidly evolving
facilities such as HTTP or an async executor can release independently. The
bundle pins a mutually tested selection without forcing all packages to share
the compiler's release cycle.

## Flexible Language, Replaceable Libraries

The language should stabilize the mechanisms required to express high-level
concepts rather than permanently selecting one high-level design.

Classes can be built from nominal structs, traits or interfaces,
implementations, constructors, and delegation. Class syntax may later elaborate
to those mechanisms. Inheritance should not be admitted until concrete use
cases demonstrate that these mechanisms are insufficient.

Async requires a stable language-level account of suspension, cancellation,
ownership across suspension, function effects, and lowering to resumable state
machines. Executors, event loops, timer wheels, and platform reactors should
remain libraries behind compatible interfaces. SlopH may ship a reference
executor without making it the only valid executor.

The same rule applies to generators, actors, serialization frameworks, and
similar abstractions: stabilize only the semantic hooks that independent
libraries cannot provide safely or interoperably.

## Size and Admission Criteria

Library size should not be governed primarily by lines of code. Each layer
instead has a semantic, dependency, trust, and compilation-cost budget:

- `core` must be completely understandable and tightly compiler-tested;
- portable foundations should avoid dependency cycles and remain cheap to
  compile;
- official packages may be larger but must remain independently replaceable;
- the bundle should be large enough for ordinary work without registry
  discovery.

A practical distribution test is that a fresh offline installation can build:

1. a command-line application;
2. a JSON-reading filesystem tool;
3. a small HTTP service;
4. an async network client;
5. tests for all of the above.

The mandatory layer should not grow to satisfy this test. The bundle satisfies
it by making official packages readily available.

## Versioning Direction

Language, compiler, mandatory library, and package compatibility must be
distinguished explicitly. A compiler release should identify the exact
mandatory-library revision it implements. Official package manifests should
declare supported language and core-library ranges, while the batteries bundle
locks exact package versions for reproducibility.

Projects must be able to vendor the selected packages and build without a
registry or pre-populated shared cache. Compiler upgrades should not silently
upgrade independently versioned libraries in a locked project.

Bundling a package with the compiler does not make it an implicit dependency.
It makes canonical source available offline through the compiler's package
root. Unused bundled packages must remain outside the build graph.

## Questions to Resolve

- The exact boundary between the mandatory `core` library and portable `std`.
- Whether `Option`, `Result`, iteration, hashing, and text are automatically in
  scope or merely always available for explicit import.
- Whether official packages ship as source, compiled artifacts, or both.
- Compatibility and support periods for the compiler-coupled library and the
  official bundle.
- Whether the default project template depends on the complete bundle or a
  smaller application profile.
- How multiple executors and platform-specific I/O implementations advertise
  compatible async capabilities.

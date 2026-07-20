# Single-Binary Command-Line Interface

This document records the command-line interface for the SlopH toolchain. The
authoritative implementation is the C11 `sloph` executable built by
`make -C src/sloph-c-bootstrap`. Sections explicitly labeled future design are
proposals rather than current behavior.

The eventual toolchain is one binary containing the compiler, checker,
formatter, inspection tools, package client, language server, and—after
v1—registry server. The concrete command syntax is provisional until an
implementation and usability tests confirm it.

## General Form

```text
sloph [global options] <command> [command options]
```

Global options:

```text
--manifest PATH
--package NAME
--registry NAME_OR_URL
--target TRIPLE
--profile dev|release
-j, --jobs NUMBER
--offline
--locked
--cache auto|off|local-only
--diagnostics human|jsonl
--color auto|always|never
-q, --quiet
-v, --verbose
--help
--version
```

Only options meaningful to a command are accepted. For example, `--target`
does not affect source formatting.

`--profile dev` is the default debug and iteration policy: complete checking,
source-level debug information, uniform generic code, and the minimal bounded
optimization pipeline. `--profile release` reuses the same checked Core and may
run bounded specialization and additional optimization. Profiles never change
source semantics, validity, diagnostics, or safety checks; they only select
code-generation, optimization, debug-information, and profile-specific artifact
policies.

## Current Compiler Pipeline

The current public stage commands name both sides of each boundary:

```text
sloph canopy-to-crown INPUT [-o CROWN_JSON]
sloph crown-to-heartwood PROJECT [-o HEARTWOOD_CORE]
sloph heartwood-to-timber INPUT --symbol GLOBAL_ID [-o TIMBER_C]
```

`canopy-to-crown` parses Source and emits deterministic Crown AST JSON.
`crown-to-heartwood` loads a strict `sloph.json` format 1 project, resolves and
checks it, and emits canonical Heartwood Core. `heartwood-to-timber` validates
Heartwood and emits deterministic portable C11. `check`, `compile`, and `run`
compose these stages.

Native provider selection accepts only strict `provider.json` format 1
metadata and reviewed local `.c`/`.S` sources. It does not execute dependency
scripts, load shared providers, or add runtime search paths.

## Input, Output, and Stability

- An input path may name a source file, manifest, project directory, or `-` for
  standard input where supported.
- Primary command output goes to standard output. Diagnostics and progress go
  to standard error and never corrupt structured standard output.
- `-o -` selects standard output; otherwise `-o PATH` selects an output file.
- Consumable inspection commands support `--format text|json|binary` where
  appropriate.
- JSON and binary documents contain a schema identity and version. Consumers
  must reject unsupported semantic versions rather than guess.
- Canonical text, JSON, and binary output is deterministic and independent of
  hash-map order, worker scheduling, and irrelevant workspace paths.
- Every supported binary semantic intermediate accepted or produced by the
  toolchain can be validated and rendered as canonical text by the shipped
  executable. Decoding is bounded and never executes embedded code. Backend
  products such as native object files and executables are excluded.
- The binary Syntax representation required in registry packages renders to
  canonical source text, not merely an AST diagnostic dump.
- A file input may use format detection. Standard input requires an explicit
  `--input-format` whenever more than one representation is accepted.
- Human diagnostics use standard error. `--diagnostics jsonl` emits one
  versioned diagnostic object per line on standard error.

Stable exit codes:

```text
0  success
1  invalid program, failed test, or requested check failed
2  invalid command invocation
3  package, registry, filesystem, toolchain, or environment failure
4  internal compiler failure
```

## Experimental Core v0 Tools

The Core v0 compatibility profile is defined in
[CORE_V0.md](../language/CORE_V0.md). It is intentionally placed below
`unstable`: it is useful for testing the representation, but it is not the
eventual stable `core` interface described later in this document.

The authoritative C11 compiler provides:

```text
sloph unstable core check INPUT

sloph unstable core print INPUT
    [-o PATH]

sloph unstable core eval INPUT
    --symbol GLOBAL_ID
    [--fuel NUMBER]
```

`INPUT` is a Core v0 tagged S-expression text file, or `-` for standard input.
No format detection is performed. `core check` parses and independently
validates the document. `core print` validates it and writes canonical Core v0
text to standard output, or to `-o PATH`. `core eval` validates the document,
evaluates the selected fully qualified global under the specified fuel bound,
and prints a canonical data value. A final function closure is not a printable
Core v0 result.

These commands use the general output separation and exit-code conventions
above, but their syntax and diagnostics may change while the profile remains
under `unstable`. Resource limits and evaluation behavior are part of the Core
v0 profile rather than ambient host-language behavior.

The experimental Core-only commands do **not** provide:

- JSON or binary Core input or output;
- a Core structural or textual `diff` command;
- macro expansion or type inference;
- optimization passes or optimized-Core output;
- C, native, WebAssembly, object, assembly, or executable backends;
- the stable public Core API and compatibility guarantees proposed below.

Requests for an unsupported format, operation, or backend are rejected rather
than silently approximated. New semantic forms require a later Core format
version; Core v0 readers reject unknown tags, sections, and fields.

## Experimental Source-to-Native v0 Tools

The first complete authored-program slice is defined by
[SOURCE_V0.md](../language/SOURCE_V0.md) and
[C_BACKEND_V0.md](./C_BACKEND_V0.md). It is also deliberately unstable. The
CLI exposes the reusable Syntax, project, compiler, and backend libraries as:

```text
sloph unstable check PROJECT

sloph unstable format INPUT
    [--write | --check | --stdout]

sloph unstable ast print INPUT
    [--input-format source|json]
    [--format json]
    [-o PATH]

sloph unstable ast check INPUT
    [--input-format source|json]

sloph unstable core print INPUT
    [--input-format text|source]
    [-o PATH]

sloph unstable compile INPUT
    [--input-format source|text]
    [--symbol GLOBAL_ID]
    -o PATH
    [--cc PATH]
    [--emit-c PATH]
    [--timings]

sloph unstable run PROJECT
    [--cc PATH]
    [--emit-c PATH]
    [--timings]
```

`PROJECT` is a project directory or `sloph.json`. Source compilation takes a
project; textual Core compilation takes one Core v0 file and requires
`--symbol`. `--cc` defaults to `cc` and is explicit rather than read from
ambient build configuration. The native bridge supports macOS ARM64 and Linux
AMD64 hosts only.

Formatting defaults to standard output. `--write` atomically replaces the
input file; `--check` writes nothing and exits 1 when the canonical spelling
differs. AST output is the versioned `sloph.syntax` JSON schema only. Native
execution prints the canonical Core data value produced by the project entry.
In JSONL diagnostic mode, a native runtime failure is wrapped as one
`compiler.runtime.failed` diagnostic instead of forwarding raw runtime stderr.
The profile has no inference, macros, floats, I/O, optimization pipeline,
cross-compilation, object-file interface, package resolver, or stable ABI.

## Library-First Implementation

The CLI is a thin frontend over reusable toolchain libraries. Command handlers
must not contain the only implementation of parsing, formatting, expansion,
Core inspection, compilation, package resolution, or artifact validation.

The frontend is responsible only for:

- parsing command-line arguments and locating configuration;
- constructing explicit library option and capability values;
- selecting files, standard input, and standard output;
- rendering structured results and diagnostics for a terminal;
- translating library outcomes into the documented process exit codes.

The reusable libraries return typed values and structured diagnostics. They do
not call process exit, parse terminal arguments, write progress directly to a
terminal, inspect ambient environment variables, or use global mutable compiler
state. Filesystem, registry, clock, process, and network access is supplied
through explicit capability interfaces. This makes the same operations usable
from the CLI, language servers, tests, build services, and user programs.

Conceptually, the public library surface is divided into small packages:

```text
syntax       parse source; represent, validate, read, and write public Syntax
format       apply the canonical formatter to Syntax and source text
expand       perform bounded transformations with explicit expansion context
core         read, write, validate, query, and compare public typed Core
interface    read, validate, query, and compare compiled module interfaces
compiler     check and compile through a configurable, observable pipeline
package      resolve manifests and lockfiles and fetch package content
artifact     identify, validate, reproduce, and inspect compiled artifacts
registry     access registries through the public client protocol
diagnostic   shared diagnostic, source-map, provenance, and rendering types
```

These names describe responsibilities and are not final registry package names.
The packages must not all depend on the complete compiler. For example, a tool
that reads Core or formats parsed Syntax should not need the native backend,
linker, package resolver, or macro executor.

The CLI commands and public libraries use the same option types, schema
encoders, validators, and semantic operations. JSON is an interchange format,
not the in-process API: library users work with typed values and explicitly
encode JSON only at a process or storage boundary.

After the language can build and publish its own libraries, supported toolchain
libraries should be distributed through the registry so users can build tools
such as:

- custom source renderers and formatters;
- AST and Core query, visualization, and auditing tools;
- Core equivalence and optimization-analysis tools;
- editor, refactoring, and documentation tools;
- package, artifact, and dependency inspectors.

The standard `format` command remains the sole canonical formatter even when
users publish alternative formatters. A custom formatter must not change what
the compiler accepts or redefine the canonical registry source identity.

Published toolchain libraries are versioned public APIs. They operate on the
same versioned public Syntax, Core, interface, artifact, and diagnostic schemas
documented by the CLI. They must accept explicit resource limits when decoding
or validating untrusted data.

Compiler-private types and passes are not exposed merely because the CLI uses
them. Operations below `internal` may use unpublished implementation libraries
or explicitly internal modules with no compatibility promise. A public package
may never require consumers to import an `internal` API to perform a documented
stable operation.

## Compilation and Execution

### Compile

```text
<lang> compile [PATH]
    [-o PATH]
    [--backend native|wasm]
    [--host native|browser|node|bun]
    [--emit executable|object|assembly|wasm|javascript|typescript]
    [--no-link]
    [--feature NAME]...
    [--all-features]
    [--no-default-features]
    [--source-only]
    [--timings]
    [--explain-rebuild]
```

`compile` compiles a source file or project. A project manifest determines
whether the default result is an executable or library. `--source-only`
disables optional precompiled registry artifacts so the canonical source build
path can be tested.

`--backend`, `--target`, and `--host` form one validated target tuple. Native
builds emit machine code and use the native host profile. Wasm builds emit a
module and the required versioned JavaScript helpers; TypeScript declarations
are generated from the same validated ABI description when requested. Browser
output is not directly runnable from a terminal without an explicit browser
harness. Node and Bun may provide CLI host runners.

`--timings` reports phase time and peak resource information.
`--explain-rebuild` reports why each compiled node was invalidated or why a
cached artifact could not be used.

### Check

```text
<lang> check [PATH]
    [--through parse|expand|resolve|type|core]
    [--all-targets]
    [--backend native|wasm]
    [--host native|browser|node|bun]
    [--include-tests]
    [--keep-going]
    [--deny-warnings]
    [--source-only]
    [--timings]
```

The default checks through validated initial Core without native code
generation. `--through` stops after the named stage.

### Run

```text
<lang> run [PATH]
    [--binary NAME]
    [compile options]
    [-- PROGRAM_ARGUMENTS...]
```

Arguments following `--` are passed unchanged to the program.

### Test

```text
<lang> test [PATH] [FILTER]
    [--list]
    [--exact]
    [--no-run]
    [--threads NUMBER]
    [--report human|jsonl]
    [-- TEST_ARGUMENTS...]
```

`--no-run` compiles tests without executing them. `--exact` treats `FILTER` as
a complete test identity rather than a substring or pattern.

## Formatting

```text
<lang> format [PATH...]
    [--write | --check | --stdout]
    [--stdin-name PATH]
```

The language has one canonical format and no configurable formatting style.
The default is `--write` for file inputs and `--stdout` for standard input.
`--check` changes no files and exits with status 1 when formatting is required.
`--stdin-name` supplies module and diagnostic context for standard input.

## Public Source AST

```text
<lang> ast print INPUT
    [--input-format source|json|binary]
    [--format text|json|binary]
    [--locations none|offset|line-column]
    [--trivia include|exclude]
    [-o PATH]

<lang> ast check INPUT
    [--input-format source|json|binary]
    [--canonical]
```

The public AST is the small, documented `Syntax` representation used by tools
and transformations. It is not the compiler's private parser data structure.

`ast check` validates parsing, schema structure, limits, and canonical form when
requested. It does not perform name resolution or type checking; use the
top-level `check` command for semantic validation.

The following pipeline must work:

```text
<lang> ast print program.lang --format json |
    <lang> ast check - --input-format json
```

## Macro Expansion

```text
<lang> expand print INPUT
    [--mode one|full]
    [--at FILE:LINE:COLUMN]
    [--format source|json]
    [--provenance none|summary|full]
    [-o PATH]
```

`--mode one` expands one invocation selected by `--at`. `--mode full` produces
the canonical fully expanded surface module. Expansion output retains enough
provenance to connect generated syntax to invocations and definitions.

## Typed Core

```text
<lang> core print INPUT
    [--input-format source|text|json|binary]
    [--stage elaborated|optimized]
    [--format text|json|binary]
    [--module NAME]
    [--symbol NAME]
    [--provenance none|summary|full]
    [-o PATH]

<lang> core check INPUT
    [--input-format source|text|json|binary]
    [--stage elaborated|optimized]

<lang> core diff LEFT RIGHT
    [--format text|json]
    [--ignore-locations]
    [--ignore-provenance]
```

`core print` emits resolved global identities, explicit types, fixed ordering,
and canonical formatting. The `elaborated` and `optimized` boundaries are
public contracts; individual optimization passes are compiler internals.

`core print --format json` is the supported Core AST dump. A separate
`core dump ast` command would duplicate this operation. Core text, JSON, and
binary representations are versioned public formats; compiler memory layouts
remain private.

`core check` validates scope, types, constructors, primitives, ownership, and
trusted-operation provenance independently of surface checking.

The following pipeline must work:

```text
<lang> core print program.lang --format json |
    <lang> core check - --input-format json
```

## Internal Compiler Commands

Everything without a compatibility guarantee lives below the `internal`
command. This keeps implementation details out of the stable top-level
namespace and makes accidental dependencies on them conspicuous.

`internal` commands may change or disappear with any compiler release. Their
structured output records the exact compiler build but has no cross-version
schema compatibility promise. It remains deterministic within one compiler
build so compiler tests can use snapshots.

Use `internal`, rather than `unstable`, for compiler-private facilities.
`unstable` is for experimental public features that may later graduate into a
supported interface; the current Core v0 tools are the first such profile.

### Pass-by-Pass Core

```text
<lang> internal core print INPUT
    --after-pass NAME
    [--format text|json]
    [--module NAME]
    [--symbol NAME]
    [-o PATH]

<lang> internal core check INPUT
    [--after-pass NAME | --after-each-pass]

<lang> internal core passes
    [--format text|json]
```

The stable `core` command exposes only elaborated and optimized Core. These
internal variants expose the compiler's current transformation pipeline.

### Lower Compiler IR

```text
<lang> internal ir print INPUT
    --stage lower|machine|assembly
    [--after-pass NAME]
    [--target TRIPLE]
    [--format text|json]
    [-o PATH]

<lang> internal ir check INPUT
    --stage lower|machine
    [--target TRIPLE]
```

Lower and machine IR are compiler-version-specific debugging representations,
not stable language interchange formats. Machine and assembly output is target
dependent. Additional private dumps must be added below `internal` rather than
weakening the AST or Core contracts.

## Compiled Module Interfaces

```text
<lang> interface print INPUT
    [--module NAME]
    [--format text|json|binary]
    [-o PATH]

<lang> interface check INPUT

<lang> interface diff OLD NEW
    [--compatibility source|abi]
    [--format text|json]
```

`interface diff` reports semantic public-interface changes separately from ABI
changes. Private implementation changes do not appear as interface changes.

## Dependency and Build Graphs

```text
<lang> graph print [PATH]
    [--kind package|module|compile|syntax|build-task]
    [--format text|json|dot]
    [-o PATH]

<lang> graph why FROM TO
    [--format text|json]
```

`graph why` reports the dependency path and dependency kind connecting two
nodes. Graph identities are stable enough for AI tooling to correlate with
compiler diagnostics and rebuild explanations.

## Package Client

### Project and Dependency Management

```text
<lang> package init [DIRECTORY]
    --name NAME
    [--kind executable|library]

<lang> package add NAME[@VERSION]
    [--dev | --build]
    [--feature NAME]...
    [--path PATH | --git URL --revision REVISION]

<lang> package remove NAME
    [--dev | --build]

<lang> package resolve
    [--update NAME]
    [--dry-run]

<lang> package fetch
    [--source-only]

<lang> package verify
    [--source-only]
    [--rebuild-artifacts]
    [--audit-claims]
    [--audit-attestations]
    [--downloads]

<lang> package info [NAME]
    [--presentation publisher|ai|all]
    [--audits]
    [--downloads]
    [--format text|json]
```

Dependency resolution writes a deterministic lockfile. `--locked` forbids
changes to it, while `--offline` forbids network access.

`package info` keeps publisher-authored and AI-generated descriptions separate
and can show audit provenance and verified download locations. `package verify`
validates self-claims, locally available attestations, and download records
under explicit trust policy. A publisher claim is never treated as registry
verification, and a download URL is never accepted without its expected hash
and size.

### Publishing

```text
<lang> package publish
    [--registry NAME_OR_URL]
    [--artifact source|interface|core|native]...
    [--dry-run]
```

Source and a complete manifest remain canonical. Interfaces, typed Core, and
native objects are optional accelerators. Publishing credentials must not be
accepted as literal command-line arguments that may leak through process lists
or shell history.

## Artifact Inspection

```text
<lang> artifact print PATH_OR_ID
    [--format text|json]
    [--include-provenance]

<lang> artifact check PATH_OR_ID
    [--verify hash|signature|schema|content|all]

<lang> artifact reproduce PACKAGE
    --kind interface|core|native
    [--compare PATH_OR_ID]
```

`artifact reproduce` rebuilds an accelerator from canonical inputs and can
compare its content identity with a published artifact.

## Audit Profiles

```text
<lang> audit [PATH]
    --profile NAME[@VERSION]
    [--profile NAME[@VERSION]]...
    [--target TRIPLE]
    [--scope package|application]
    [--format human|json|sarif]
    [--evidence none|summary|full]
    [--evidence-source package|registry|local|all]
    [--deny conditional|unknown|not-applicable]...
    [-o PATH]

<lang> audit profile list
    [--format human|json]

<lang> audit profile show NAME[@VERSION]
    [--resolved]
    [--format human|json]
```

An audit profile is a versioned ordered list of requirement identities,
parameters, and accepted result states. Requirements report `pass`, `fail`,
`conditional`, `unknown`, or `not-applicable`; an unproven state never silently
becomes a pass. Package scope may retain machine-checkable obligations on its
callers. Application scope resolves the locked graph, generic arguments,
providers, runtime helpers, target backend, and foreign boundaries before
attempting to discharge those obligations.

Profiles are resolved from declared and locked inputs without ambient network
access. Multiple `--profile` options compose their requirements and apply the
strictest compatible result policy. Conflicting requirement parameters are a
profile-resolution error rather than an order-dependent choice.

The official Power-of-Ten profile, if admitted, is an evidence-producing SlopH
interpretation of the original C rules. Its result is not presented as NASA/JPL
certification. Project profiles may extend it with requirements such as a
static memory profile, bounded collection capacity, or an FFI allowlist. The
exploratory design is recorded in
[Audit Commands and Requirement Profiles](../../idea/AUDIT_PROFILES.md).

## Cache and Build Output Management

```text
<lang> cache status
    [--format text|json]

<lang> cache gc
    [--max-size SIZE]
    [--max-age DURATION]
    [--dry-run]

<lang> cache clear
    [--kind ast|interface|core|native|all]
    [--yes]

<lang> clean [PATH]
    [--target TRIPLE]
    [--profile dev|release]
```

`clean` removes project build outputs. Cache deletion remains explicit under
`cache`. Deleting any cache may affect performance but not correctness or
available language behavior.

## Tool Integration

```text
<lang> lsp --stdio

<lang> target list
    [--format text|json]

<lang> target print TRIPLE
    [--format text|json]

<lang> doctor
    [--format text|json]

<lang> version
    [--verbose]
    [--format text|json]

<lang> help [COMMAND...]
```

`doctor` inspects the compiler, target tools, registry configuration, cache,
and environment without changing them. `lsp --stdio` runs the language server
over the standard Language Server Protocol transport.

## Future Registry Server: V2 or Later

V1 includes the registry client but does not need to host a registry. The
top-level `registry` name is reserved for a later self-hosted registry and
verified local-cache server implemented by the same binary.

### Initialize Storage

```text
<lang> registry init DIRECTORY
    --name NAME
    [--public-url URL]
```

This creates registry storage metadata without starting a service.

### Run a Verified Local Cache

```text
<lang> registry serve DIRECTORY
    --mode cache
    --upstream URL
    [--listen ADDRESS]
    [--max-size SIZE]
    [--offline]
    [--read-only]
    [--config PATH]
```

Cache mode is a read-through proxy for an upstream registry. It validates
package hashes, signatures, schemas, and artifact compatibility before storing
or serving content. It can serve already verified content while offline. A
missing item is an ordinary cache miss, and an unavailable binary accelerator
falls back to a compatible lower-level artifact or canonical source.

The default listen address is loopback. Non-loopback service requires explicit
security configuration.

### Run an Authoritative Registry

```text
<lang> registry serve DIRECTORY
    --mode origin
    [--listen ADDRESS]
    [--public-url URL]
    [--read-only]
    [--config PATH]
```

Origin mode stores and serves independently published packages. Publishing
requires configured authentication; public downloads may be anonymous.
Authentication, TLS, publisher identity, signing, and transparency protocols
remain v2 design decisions and must not be accidentally fixed by the v1 client.

### Registry Administration

```text
<lang> registry status DIRECTORY
    [--format text|json]

<lang> registry verify DIRECTORY
    [--package NAME[@VERSION]]
    [--format text|json]

<lang> registry sync DIRECTORY
    --upstream URL
    [--package NAME[@VERSION]]...
    [--artifact source|interface|core|native]...
    [--dry-run]

<lang> registry gc DIRECTORY
    [--max-size SIZE]
    [--max-age DURATION]
    [--dry-run]
```

The v1 package client accepts registry aliases or URLs so it can use these
future services without compiler changes.

## Conformance Requirements

For the current experimental v0 milestone, conformance is limited to:

- parsing and validating canonical tagged S-expression Core v0 text;
- canonical text printing and parse-print idempotence;
- deterministic, fuel-bounded evaluation of the selected global;
- deterministic rejection of malformed, invalid, cyclic, over-limit, and
  unsupported inputs;
- standard-input/output separation and the documented process exit classes;
- identical results from the CLI and the public C library operation it invokes;
- Source parsing, AST JSON, canonical formatting, project resolution, and
  deterministic Source-to-Core lowering under the Source v0 restrictions;
- deterministic C11 emission and native execution, including higher-order and
  dynamic function cases, on the two documented host targets;
- deterministic runtime resource failures.

Core JSON, binary formats, diff, optimization, higher-order native code, and
other backends are outside the v0 profile. Source and C11 results count toward
the experimental v0 milestone, but must not be mislabeled as Core v0 format
conformance.

The eventual supported CLI conformance suite must verify:

- AST JSON round-trips through `ast print` and `ast check`;
- Core JSON round-trips through `core print` and `core check`;
- AST, expansion, Core, interface, graph, and diagnostic output is
  deterministic across worker counts and workspace paths;
- structured primary output is never mixed with human diagnostics;
- exit codes retain their documented meanings;
- malformed and incompatible structured inputs fail within resource limits;
- public schemas do not expose private compiler representations;
- compiler-private passes and lower IR are reachable only through `internal`;
- no command below `internal` is presented as a cross-version contract;
- CLI commands and direct library calls produce semantically identical results
  and structured diagnostics for the same explicit inputs;
- public toolchain libraries perform no undeclared ambient I/O and never exit
  the hosting process;
- the syntax, formatting, and Core libraries can be used without linking the
  native backend or package manager;
- source-only compilation succeeds without registry accelerators or populated
  caches;
- cache and registry failures cannot change program meaning;
- future registry cache mode rejects corrupt artifacts and safely falls back to
  canonical source.

## Deferred Decisions

- The language and executable name.
- Exact JSON and binary schemas.
- Package manifest, lockfile, and configuration-file syntax.
- Authentication, signing, and transparency mechanisms.
- Whether any lower IR becomes a supported public interchange format.
- Shell-completion and documentation-generation commands.
- The implementation schedule for v2 registry hosting.

# Single-Binary Command-Line Interface

This document records the proposed command-line interface for the language
toolchain. The examples use `<lang>` until the language and executable receive
final names.

The eventual toolchain is one binary containing the compiler, checker,
formatter, inspection tools, package client, language server, and—after
v1—registry server. The concrete command syntax is provisional until an
implementation and usability tests confirm it.

## General Form

```text
<lang> [global options] <command> [command options]
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

The first executable milestone is the experimental Core v0 profile defined in
[CORE_V0.md](../language/CORE_V0.md). It is intentionally placed below
`unstable`: it is useful for testing the representation, but it is not the
eventual stable `core` interface described later in this document.

The Python 3.11+ implementation has no third-party runtime dependencies and
provides:

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
v0 profile rather than ambient Python behavior.

The experimental profile does **not** provide:

- JSON or binary Core input or output;
- a Core structural or textual `diff` command;
- source-language parsing, elaboration, macro expansion, or type inference;
- optimization passes or optimized-Core output;
- C, native, WebAssembly, object, assembly, or executable backends;
- the stable public Core API and compatibility guarantees proposed below.

Requests for an unsupported format, operation, or backend are rejected rather
than silently approximated. New semantic forms require a later Core format
version; Core v0 readers reject unknown tags, sections, and fields.

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
    [--emit executable|object|assembly]
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

`--timings` reports phase time and peak resource information.
`--explain-rebuild` reports why each compiled node was invalidated or why a
cached artifact could not be used.

### Check

```text
<lang> check [PATH]
    [--through parse|expand|resolve|type|core]
    [--all-targets]
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

<lang> package info [NAME]
    [--format text|json]
```

Dependency resolution writes a deterministic lockfile. `--locked` forbids
changes to it, while `--offline` forbids network access.

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

For the current experimental Core v0 milestone, conformance is limited to:

- parsing and validating canonical tagged S-expression Core v0 text;
- canonical text printing and parse-print idempotence;
- deterministic, fuel-bounded evaluation of the selected global;
- deterministic rejection of malformed, invalid, cyclic, over-limit, and
  unsupported inputs;
- standard-input/output separation and the documented process exit classes;
- identical results from the CLI and the Python library operation it invokes.

JSON, binary, diff, optimizer, source-language, and backend cases are outside
the Core v0 profile and must not be reported as passing Core v0 conformance.

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

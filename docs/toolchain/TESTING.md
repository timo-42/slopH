# Shared Language and Toolchain Testing

This document defines the test architecture for the language, Core, compiler,
backends, and future bootstrap chain.

The normative test corpus is implementation-neutral. The authoritative C11
compiler, a later self-hosted compiler, and the future reproducible-bootstrap
compiler must consume the same applicable test cases. Removed implementation
tests do not define language behavior.

## Principles

- Organize tests by stable specification boundary rather than compiler module.
- Store shared cases as data and source files, never executable adapter code.
- Run the same applicable cases through every compiler implementation.
- Keep tests of private implementation details separate from conformance tests.
- Make failures identify the first boundary at which behavior diverges.
- Use deterministic, versioned representations for public AST, expansion,
  Core, interfaces, diagnostics, and other machine-consumable results.
- Do not require network access, registry state, or a populated cache.
- Treat resource-limit, malformed-input, and determinism tests as conformance
  requirements rather than optional robustness tests.

## Experimental v0 Test Profile

The first implemented profile tests the tagged S-expression text format in
[CORE_V0.md](../language/CORE_V0.md), the authored profile in
[SOURCE_V0.md](../language/SOURCE_V0.md), and the direct C11 bridge in
[C_BACKEND_V0.md](./C_BACKEND_V0.md). Its operations are:

```text
sloph unstable core check
sloph unstable core print
sloph unstable core eval
sloph unstable check
sloph unstable format
sloph unstable ast print
sloph unstable core print --input-format source
sloph unstable compile
sloph unstable run
```

The stable Source v1 profile is additionally exercised through the supported
commands:

```text
sloph check
sloph format
sloph run
```

Shared cases live under `tests/core/**/case.test`,
`tests/source/**/case.test`, and `tests/v1/**/case.test`. They are data, not
executable test programs, and use these exact case keys:

```text
format: 0
name: core/valid/integer-add
kind: core-eval
input: input.core
symbol: example::main
fuel: 1000
expect-exit: 0
expect-output: expected.core-value
expect-diagnostics: diagnostics.txt
```

The fixed field catalog is:

- `format`, `name`, `kind`, `input`, and `expect-exit` are required;
- `symbol` is permitted for `core-eval` and `core-run` and selects the fully
  qualified global;
- `fuel` is permitted only for `core-eval` and sets its evaluation bound;
- `expect-output` and `expect-diagnostics` optionally name golden files;
- `kind` is exactly `core-check`, `core-print`, `core-eval`, `core-run`,
  `source-check`, `source-format`, `source-ast`, `source-core`, `source-run`,
  `v1-check`, `v1-format`, `v1-ast`, `v1-core`, `v1-run`, or `v1-native`.

`expect-exit` is in the tool-status range `0..4` except for `v1-native`, which
accepts the full portable process-status range `0..255`.

The `v1-*` kinds run stable Source v1 commands: `v1-check` runs `sloph check`,
`v1-format` runs `sloph format --stdout`, `v1-ast` runs `sloph ast print`,
`v1-core` runs `sloph core print --input-format source`, `v1-run` runs
`sloph run`, and `v1-native` compiles then directly executes the produced
program so exact application exit statuses remain observable. Output and
structured diagnostics are compared exactly like the corresponding
`source-*` kinds.

Unknown or duplicate fields, missing required fields, invalid values, and fields
not permitted for the selected kind are case-format errors. Referenced paths are
relative to the case directory. Golden output is compared as exact UTF-8 text;
the runner does not normalize different Core forms or diagnostics into
equality.

The authoritative adapter and implementation-specific C tests run through the
root Makefile:

```text
make test
make cases
make smoke
```

Bundled-library behavior is tested beside each library rather than through a
compiler implementation's unit tests. Each source module has an independently
compilable project under that library's `tests/` directory, using the source
module's filename. The project prints deterministic results to standard output,
and its library test runner compares those bytes exactly with `expected.txt`.
Run all bundled-library tests against the authoritative compiler with:

```text
make cases
```

`make cases` runs every portable and library suite; `make check` composes unit,
portable, library, and stage-smoke gates; `make sanitize` runs the C unit and
CLI tests with AddressSanitizer and UndefinedBehaviorSanitizer. Individual
`core-cases`, `source-cases`, `native-cases`, and `library-cases` targets remain
available in `src/sloph-c-bootstrap/Makefile` for narrow work.

Core cases cover text parsing, validation, canonical printing, evaluation,
resource limits, diagnostics, and CLI behavior. Source cases use either a
single `.sloph` input for formatting and AST output or a project directory for
checking, elaborated Core, and native execution. The profile does not cover
binary serialization, Core diffing, optimization, ownership, effects,
WebAssembly, or other backends. Such a case is outside the advertised profile,
not a skipped or passing v0 case.

This narrow profile is experimental. It does not make the broader future test
layout or stable Core boundaries below part of the current implementation.

## Directory Layout

```text
tests/
├── language/                  # End-to-end source-language behavior
│   ├── valid/
│   ├── invalid/
│   └── runtime/
│
├── syntax/                    # Source -> public Syntax AST
│   ├── lexer/
│   ├── parser/
│   └── formatter/
│
├── desugar/                   # Surface transformations
│   ├── standard-sugar/
│   ├── macros/
│   ├── operators/
│   └── provenance/
│
├── elaboration/               # Resolved typed source -> initial Core
│   ├── names/
│   ├── types/
│   ├── ownership/
│   └── source-to-core/
│
├── core/                      # Independent Core behavior
│   ├── parse/
│   ├── validate/
│   ├── invalid/
│   ├── evaluate/
│   ├── optimize/
│   └── serialization/
│
├── backend/                   # Core -> executable behavior
│   ├── c/
│   ├── native/
│   └── equivalence/
│
├── bootstrap/                 # Future reproducible bootstrap chain
│   ├── seed-c90/
│   ├── rv32im/
│   ├── b0/
│   ├── bootstrap-core/
│   ├── source-core-match/
│   └── fixed-point/
│
└── cli/                       # Stable CLI input/output cases
```

Directories may be added only for a distinct test boundary. The repository
should not create empty future directories merely to match this illustration.
Implementation-specific test code and corpus adapters live under
`tests/implementation/<implementation>/`. Keeping them outside compiler source
trees makes the normative corpus and repository test entry points independent
of any one bootstrap implementation.

## Boundary Model

```text
syntax       source -> public Syntax AST
desugar      Syntax AST -> canonical kernel surface
elaboration  resolved typed surface -> initial typed Core
core         Core -> validated, optimized, or evaluated Core behavior
backend      validated Core -> target executable behavior
bootstrap    C90 seed -> fixed-point self-hosted compiler
language     authored source -> observable language result
```

Each suite isolates one contract. End-to-end language cases cross all relevant
boundaries, but they do not replace focused boundary tests.

### Language

Language tests define observable source-language behavior. They cover accepted
programs, rejected programs, and runtime behavior without requiring a specific
internal AST, optimization sequence, or machine instruction selection.

A complete compiler must pass all language cases applicable to its declared
feature profile. The future bootstrap compiler may initially implement only the
explicit Bootstrap-Core or bootstrap-source profile, but it must run the same
cases used by other implementations for that profile.

### Syntax

Syntax tests cover tokenization, parsing, public AST serialization, source
locations, trivia preservation, syntax errors, and canonical formatting.

Formatter cases require:

- the exact canonical formatted source;
- idempotence after a second formatting pass;
- preservation of program meaning;
- stable handling of comments and source trivia;
- deterministic output across implementations.

### Desugaring

Desugaring tests begin with valid public Syntax and end with canonical kernel
surface syntax. They separately cover:

- standard-library sugar;
- user transformations and macros;
- operator transformation;
- one-step and complete expansion;
- hygiene and resolved capture behavior;
- expansion provenance and diagnostic source mapping;
- expansion depth, generated-size, and resource limits.

Desugaring tests do not perform type checking except where a transformation's
documented input contract requires syntax categories. Generated output is
checked by the ordinary later pipeline in separate integration cases.

Illustrative case:

```text
tests/desugar/operators/add/
├── case.test
├── input.lang
└── expected.lang
```

### Elaboration

Elaboration tests cover name resolution, public and private identities, type
checking, ownership checking, typeclass or witness selection, and lowering into
initial typed Core.

Source-to-Core cases pin only the stable elaborated Core boundary. They must not
pin optimizer passes, private annotations, allocation choices, or backend IR.

Illustrative case:

```text
tests/elaboration/source-to-core/bool-case/
├── case.test
├── input.lang
└── expected.core
```

### Core

Core tests are independent of surface syntax, macros, and source type
inference. They directly exercise the public Core parser, serializer, validator,
reference evaluator, and stable optimizer boundary.

For the current implementation, only the experimental Core v0 text, validator,
and evaluator subset described above exists. The rest of this subsection
describes the eventual supported Core suite.

The Core suite includes:

- accepted canonical Core;
- structurally malformed Core;
- scope and type violations;
- invalid constructor, primitive, ownership, or provenance use;
- text, JSON, and binary serialization round trips;
- evaluator behavior;
- permitted stable optimizations and semantic equivalence;
- decoder and validator resource limits.

Illustrative invalid case:

```text
tests/core/invalid/wrong-application-type/
├── case.test
├── input.core
└── diagnostics.json
```

Pass-by-pass optimizer tests, private annotations, lower IR, and machine IR do
not belong in this shared Core suite. They live under implementation-specific
internal tests because those representations have no compatibility guarantee.

### Backend

Backend tests begin with independently validated Core. This prevents parser,
macro, or type-checker failures from being misreported as backend failures.

Equivalence cases compare:

- the reference Core evaluator;
- the portable C backend;
- every supported native backend;
- compile-time evaluation where applicable.

The observable result includes exit status, standard output, standard error,
declared files, traps, and deterministic runtime errors. Target-specific ABI and
object-format tests remain separate from language-semantic equivalence tests.

### Bootstrap

Bootstrap tests implement the stages in [BOOTSTRAP.md](./BOOTSTRAP.md) and are
separate from ordinary language and compiler tests. They cover:

- compiling and validating the single C90 seed;
- RV32IM instruction, assembler, image, trap, and resource behavior;
- B0 compilation and self-compilation;
- Bootstrap-Core validation and lowering;
- correspondence between compiler source and checked-in compiler Core;
- target-independent cross-host artifact equality;
- native self-compilation and byte-identical fixed points.

A bootstrap failure must identify the first divergent stage. It must not be
reported merely as a general language conformance failure.

### CLI

CLI tests cover the stable contract in [CLI.md](./CLI.md), including argument
parsing, nested subcommands, standard-input and standard-output behavior,
diagnostic separation, structured schemas, and exit codes.

CLI tests exercise the same library operations as direct-library tests. A CLI
case must not become the only test of a language or Core behavior.

### Implementation-Specific Tests

Tests under `src/sloph-c-bootstrap/tests/` may inspect private C structures and
helpers. The adapters under `tests/implementation/c/` run the portable corpus.

Equivalent self-hosted implementation tests may later live in a separate
implementation-specific directory. They cannot change the expected results of
the shared suites.

## Portable Case Format

Every shared case is a directory containing a small `case.test` descriptor and
referenced input or expected-output files. No shared case executes arbitrary
host code. This bounded line format is a test-harness protocol, not a package
manifest; projects use strict `sloph.json` format 1.

Illustrative runtime case:

```text
tests/language/runtime/integer-add/
├── case.test
├── main.lang
├── stdout.txt
└── stderr.txt
```

Illustrative manifest:

```text
name: language/runtime/integer-add
kind: run
source-root: .
entry: main.lang
expect-exit: 0
expect-stdout: stdout.txt
expect-stderr: stderr.txt
```

The test descriptor is deliberately smaller than a package manifest:

- UTF-8 text;
- one `key: value` field per line;
- fixed documented keys;
- no executable expressions;
- no interpolation, anchors, nested objects, or implicit type coercion;
- large or multiline data stored in referenced files;
- paths relative to the case directory;
- duplicate or unknown keys rejected unless a format version explicitly
  permits them.

The complete cross-profile key catalog and escaping rules remain to be
specified. The implemented v0 subset is fixed in
[Experimental v0 Test Profile](#experimental-v0-test-profile). Any
future additions must remain small enough to implement in C, the self-hosted
language, B0, or Bootstrap Core without a general-purpose data-format
dependency.

## Shared Runners

A runner adapts one compiler implementation to the shared case protocol:

```text
shared case data
       |
       +-> C runner -----------> authoritative C compiler libraries
       |
       +-> self-hosted runner -> self-hosted compiler libraries
       |
       +-> bootstrap runner ---> bootstrap-profile compiler
```

Runners may call implementation libraries directly. They must not reimplement
language rules, normalize away semantic differences, or maintain separate
expected results.

Every runner reports a common result containing at least:

- case identity and suite;
- implementation and version;
- declared feature profile and target;
- pass, fail, skip, or infrastructure-error status;
- first failed boundary;
- expected and actual artifact identities;
- structured diagnostics;
- time and resource measurements when requested.

A skip is permitted only when the manifest declares a feature or target outside
the implementation's advertised profile. Missing functionality inside an
advertised profile is a failure, not a skip.

## Golden Data

Golden outputs are used only at stable public boundaries:

- public Syntax AST;
- canonical formatted source;
- canonical fully expanded surface syntax;
- elaborated typed Core;
- stable optimized Core when that boundary is specified;
- public interfaces;
- structured diagnostics;
- observable runtime results.

Golden output may not expose:

- implementation-private object representations;
- hash-map iteration order;
- temporary or absolute paths;
- compiler-private optimizer passes;
- lower or machine IR;
- memory addresses;
- timestamps or undeclared environment state.

Golden updates must show reviewable expected-output diffs. A bulk acceptance
command cannot silently replace expected behavior.

## Diagnostic Comparisons

Diagnostic cases compare stable structured information:

- diagnostic code;
- severity;
- primary and secondary spans;
- stable message identifier and typed arguments;
- macro or transformation provenance;
- related declarations and suggestions where specified.

Human prose may also have golden tests for readability, but wording changes do
not redefine semantics unless the CLI specification explicitly makes the text
stable. Paths and source positions use canonical source identities.

## Determinism and Normalization

All runners normalize only representation details declared non-semantic by a
public schema. They must not normalize different constructors, types,
primitives, ownership states, effects, diagnostic codes, exit statuses, or
runtime values into equality.

Shared tests run with:

- fixed UTF-8 input and normalized line endings;
- stable source identities independent of checkout path;
- deterministic module and diagnostic ordering;
- declared target and feature profiles;
- empty caches in designated cold cases;
- network access disabled;
- controlled environment variables and locale;
- bounded time, memory, expansion, recursion, and output sizes.

The same canonical test must produce the same target-independent artifacts
under the C and future self-hosted implementations. Bootstrap-profile equality is
required for every construct in that profile.

## Admission Rule

A language, Core, macro, backend, CLI, or bootstrap feature is incomplete until
its stable behavior is expressible in the shared corpus and passes every
applicable implementation.

Implementation-specific tests may supplement shared cases but cannot substitute
for them. Tests that require a private representation are evidence about one
compiler implementation, not part of the language contract.

## Deferred Decisions

- Extensions to `case.test` beyond the implemented Core v0 field catalog and
  format version `0`.
- The first implementation of the common runner result schema.
- Feature-profile and target-selection notation.
- Golden-update CLI commands and review workflow.
- Test sharding, parallel execution, and resource-budget defaults.
- Which stable optimizer results, if any, become normative golden boundaries.
- How the corpus is packaged for consumption outside the compiler repository.

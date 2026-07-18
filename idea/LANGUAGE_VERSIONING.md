# IDEA: Language Editions and Versioned Core

Status: exploratory, non-normative.

SlopH should assume that source rules, Core semantics, and compiled interfaces
will eventually require incompatible corrections. Every package must therefore
select a language edition explicitly, and every serialized Core or compiled
artifact must identify the exact semantic and schema versions under which it
was produced.

The preferred direction is closer to Rust editions than to a global C-style
`--std=c99` compiler mode. Editions permit gradual migration and allow packages
written using different source editions to coexist in one dependency graph.
SlopH should additionally version Core independently because Core is a public,
validated compiler boundary rather than only an internal implementation detail.

## Why Not Only a C-Style Mode

A compiler option such as:

```text
sloph build --std=sloph2028
```

is simple and useful for isolated source files. As the primary mechanism,
however, it makes the meaning of a package depend on the invoking command and
makes a mixed-version dependency graph difficult. A build tool must either
compile every dependency in one global mode or reconstruct a separate flag for
each package.

The authoritative selection should instead live in the package manifest:

```text
language-edition = "2028"
```

The CLI may offer an edition option for single-file commands, experiments, and
manifest creation, but it must not silently override a package's declared
edition during an ordinary build.

## Source Editions

An edition defines the interpretation of source text, including:

- grammar and reserved words;
- name resolution, imports, and prelude behavior;
- type checking and inference rules;
- macro expansion and hygiene rules;
- desugaring into typed Core;
- source-visible evaluation and effect semantics.

Edition identifiers should be simple monotonically increasing years or
integers rather than semantic versions. For example:

```text
2028
2031
```

A new compiler continues to accept supported older editions. Selecting a newer
edition is explicit and should be assisted by a deterministic migration tool.
Formatting source must not change its edition or semantics.

The edition is selected per package, not per workspace and normally not per
file. A dependency written in edition 2028 can coexist with an application
written in edition 2031. Their boundary is the resolved compiled module
interface and versioned Core representation, not shared interpretation of
source syntax.

## Compiler and Toolchain Versions

The compiler release version is separate from the language edition. A compiler
may implement several editions, and multiple compiler releases may implement
the same edition.

A package manifest may declare a minimum compiler version when it relies on a
new implementation capability that does not require a new edition:

```text
minimum-compiler = "1.7"
```

Lockfiles and build diagnostics should report the selected compiler, language
edition, mandatory-library revision, and dependency versions separately.
Changing the compiler must not silently change the package's edition.

## Core Semantic and Schema Versions

Core needs two independently visible version concepts:

1. **Core semantics** define typing, evaluation, primitive behavior, and the
   meaning of Core forms.
2. **Core schema** defines the concrete serialized representation.

A schema can change without changing meaning, and a semantic correction may
require a new semantic version even if the serialized shape is unchanged.
Conceptually, a Core document or artifact records:

```text
core-semantics = 2
core-schema = 4
```

The compiler must reject unsupported versions rather than guessing. Conversion
between versions is an explicit validated compiler pass. A converter must
either preserve behavior according to documented rules or return a diagnostic
that manual migration is required.

Compiled module interfaces, cached objects, native artifacts, macro artifacts,
and runtime objects also need schema and compatibility identifiers. These
versions are not necessarily user-facing language editions and may advance
more frequently.

## Correcting Mistakes

Versioning must distinguish three cases:

- **Implementation bug:** the compiler disagrees with the existing normative
  edition. Fix the compiler. Programs relying on the bug were never portable,
  but the release and diagnostic should identify the correction.
- **Ambiguous or undesirable source rule:** retain the old interpretation for
  the old edition and introduce the corrected rule in a new edition, with a
  lint and automatic migration where possible.
- **Unsound or unsafe specified behavior:** safety may require correcting an
  existing edition. Such a correction must be rare, prominently documented,
  mechanically detected where practical, and never silently reinterpret an
  already serialized Core artifact.

Not every feature requires a new edition. Backwards-compatible additions can
become available under an existing edition when they do not change the meaning
of previously valid programs. A compiler must not use this exception to add a
new contextual interpretation that makes old source ambiguous.

## Mandatory Library Compatibility

The mandatory `sloph`, `core`, and `prelude` packages are coupled to language
and Core semantics but have their own explicit revision. An edition defines
their compatible range or exact major revision. The compiler distribution
supplies matching implementations and rejects incompatible replacements.

Independently versioned standard and batteries packages are not part of the
edition. They declare compatible language editions, Core-library revisions,
and minimum compiler versions in their manifests. A locked project does not
receive new library behavior merely because its compiler was upgraded.

## Migration Workflow

Edition migration should be an explicit operation conceptually similar to:

```text
sloph migrate --edition 2031
```

The migration tool should:

1. compile the package under its current edition;
2. enable future-edition compatibility diagnostics;
3. make deterministic source transformations;
4. format and compile the result under the target edition;
5. run the package's tests when requested;
6. update the manifest only after successful validation.

Migration must operate package by package so a large dependency graph need not
move to a new edition atomically.

## Compatibility Goal

The desired model is:

```text
package source --selected edition--> typed Core --versioned semantics/schema-->
compiled interface and target artifacts
```

Source compatibility is governed by editions. Cross-package compatibility is
governed by resolved interfaces and Core semantics. Binary and cache
compatibility is governed by artifact schemas and target ABI identifiers.
Keeping these axes separate prevents either the compiler version or a single
global `--std` flag from carrying several incompatible meanings.

## Questions to Resolve

- Whether edition identifiers use years or monotonic integers.
- How long a compiler release must support old editions and Core semantics.
- Whether published packages may select editions newer than the registry's
  current baseline.
- Which compatible additions are permitted within an existing edition.
- The exact policy for correcting specified but unsafe behavior.
- Whether Core converters are maintained indefinitely or only across adjacent
  semantic versions.
- Whether the first stable source edition and stable Core semantics share a
  number or deliberately use distinct namespaces.

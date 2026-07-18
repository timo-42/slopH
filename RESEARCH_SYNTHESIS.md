# Research Synthesis: A Small Extensible Language for AI

Research summary as of July 2026. This document connects the detailed findings
in:

- [Research: Haskell and GHC Core](RESEARCH_GHC_CORE.md)
- [Research: Extensible Programming Languages](RESEARCH_EXTENSIBILITY.md)
- [Research: Lisp and Forth](RESEARCH_LISP_FORTH.md)

The linked reports contain the primary papers, official documentation, and
practitioner sources behind each conclusion. No unauthorized repositories were
used.

## Executive Decision

Use a **fixed, strict, explicitly typed Core** and a **bounded hygienic surface
macro system**. Make the surface extensible, but do not make parsing, typing,
optimization, or lowering user-extensible.

```text
fixed source grammar
        |
        v
hygienic deterministic expansion ----> expansion/provenance view
        |
        v
name resolution + fixed type system
        |
        v
small explicitly typed Core ----------> Core validator after every pass
        |
        v
fixed bounded optimization
        |
        v
operational lower IR + code generation
```

This combines the most durable ideas from GHC, Scheme/Racket, Lisp, Forth,
staged programming, and modular syntax research while excluding the mechanisms
that most often destroy compactness or predictability.

## Ranked Findings

### 1. A small Core is valuable, but constructor count is misleading

GHC demonstrates that a stable typed Core is an excellent semantic center and
optimization language. Core Lint is especially worth copying: validate scope,
types, representations, and invariants after every lowering or optimization
pass.

However, GHC also shows that complexity moves into types, coercions, primitives,
evaluation semantics, optimizer policy, interface files, lower IRs, and the
runtime. “Nine forms” is not a nine-rule language.

**Decision:** define smallness by the complete normative specification and
primitive catalog, not the number of AST constructors.

### 2. Surface extensibility works when the semantic center stays fixed

Scheme and Racket show that hygienic macros can provide real language growth.
SugarJ shows the value of explicit syntax imports. Rust procedural macros show
the value of obvious invocation boundaries. Lisp shows the productivity of
domain vocabularies.

Systems become much larger when users can add token classes, arbitrary grammar,
binding rules, type rules, compiler passes, or private AST dependencies.

**Decision:** macros produce ordinary syntax, which then passes through the one
resolver, type checker, and Core lowering path. Extensions never redefine what
valid Core means.

### 3. Compile-time execution must be less powerful than run time

Template Haskell, Rust build/procedural macros, Lisp reader evaluation, and Forth
compiler words all demonstrate the cost of ambient authority. Filesystem,
environment, clock, network, process, and random access make builds harder to
cache, reproduce, secure, and reason about. Unbounded execution can hang the
compiler or generate enormous programs.

**Decision:** macro execution is pure, deterministic, and bounded by normative
limits for steps, recursion depth, memory, and output size. External generation
is a separate declared build task, not a macro capability.

### 4. Fixed parsing is more valuable than arbitrary notation

Lisp succeeds partly because parenthesized structure prevents most grammar
composition problems; it becomes less predictable when reader macros mutate
reading. Forth's immediate words make generic tooling dependent on execution.
SugarJ, ableC, and mixfix research show that modular grammar is possible but
requires substantial parser and composition machinery.

**Decision:** keep tokenization, delimiters, declaration shapes, and macro
invocation boundaries fixed. Permit only a restricted infix operator class with
explicit precedence and associativity from a small fixed domain.

### 5. Expansion visibility is part of the language, not optional tooling

Macro systems transfer complexity from authored source into generated source.
Without provenance and inspection, errors expose implementation details and AI
agents cannot reliably connect a surface form to its cost or behavior.

**Decision:** every generated node retains both definition-site and call-site
provenance. The compiler provides stable commands to show one-step expansion,
full canonical expansion, typed Core, and lower IR. Diagnostics point to the
authored form and include an expansion trace on demand.

### 6. Extensibility needs a convergence mechanism

Lisp and Forth show that bottom-up language growth creates powerful local
vocabularies but can fragment ecosystems. If basic iteration, resource handling,
errors, and data declarations differ in every project, the language is no longer
small for readers or AI; the missing documentation has moved into dependencies.

**Decision:** define a mandatory standard surface profile for ordinary idioms.
Syntax imports are explicit and non-transitive. Package interfaces record which
syntax providers were used and expose canonical expanded declarations.

## Proposed Version 1 Contract

### Surface language

- UTF-8 source with a fixed token grammar.
- A fixed set of delimiters and declaration forms.
- Ordinary function application has one canonical spelling.
- A small standard infix table; user operators select from a predefined operator
  token class and declare precedence and associativity.
- No reader macros, lexer modes, layout extensions, or user grammar productions.

### Macro system

- Macro invocations are recognizable without running user code.
- Macros consume and produce a stable public `Syntax` data type, not compiler
  internals or raw text.
- Lexical hygiene is automatic. Deliberate capture requires a conspicuous unsafe
  primitive, preferably unavailable outside trusted standard modules in v1.
- Syntax providers must be explicitly imported; syntax imports do not re-export
  transitively by default.
- There are exactly two stages: compile time and run time.
- Macro execution is pure and deterministic.
- Step, depth, allocation, and generated-size limits are normative and produce
  deterministic diagnostics.
- A macro cannot define syntax that changes parsing or expansion rules for the
  remainder of its own module.
- Macro output is resolved and type-checked normally. It cannot emit pre-typed
  nodes or bypass Core validation.

The semantic model can remain this small:

```text
expand : ExpansionEnv -> Syntax -> Result<Syntax, Diagnostic>
```

`ExpansionEnv` contains only explicitly imported syntax definitions and stable
compiler-provided values. It contains no ambient operating-system capabilities.

### Type system and Core

- Strict evaluation by default.
- Explicitly typed Core with term variables, literals, application, lambda,
  non-recursive and recursive binding, case analysis, types, and the minimum
  representation operations actually required.
- Avoid general equality coercions, type families, GADTs, impredicativity, and
  dependent typing in v1.
- If interfaces/typeclasses are retained, use one coherent instance model: no
  overlapping instances and no orphans. Dictionary passing may be an
  implementation model but should not expand the public type theory.
- Publish a complete finite catalog of primitives, representations, calling
  conventions, traps, and effects. This catalog counts toward language size.
- Use a distinct operational lower IR for allocation, calls, layout, and machine
  operations rather than bloating Core.

### Compiler policy

- Validate Core after desugaring and after every transformation in development
  and conformance modes.
- Keep optimizer passes fixed, deterministic, and bounded. Do not expose phase
  numbers, rewrite fuel, or compiler-private occurrence information as language
  semantics.
- Make separate-compilation summaries compact and versioned.
- Specify observable evaluation, overflow, allocation failure, and foreign-call
  behavior; do not leave them implicit in the runtime.
- Test surface programs both before and after expansion, then test typed Core and
  backend equivalence for transformations.

## Assessment of the Current Design Files

### Keep

The direction already present in [CORE.md](CORE.md) and [MACRO.md](MACRO.md) is
well supported by the research:

- one canonical small Core;
- surface sugar lowering to existing primitives;
- user-defined hygienic syntax rather than raw text substitution;
- explicit imports and a mandatory standard profile;
- pure compile-time computation;
- restricted symbol/operator extension;
- inspectable expansion;
- type checking after expansion.

### Tighten

The following should become normative before implementation:

1. State exactly two phases and define which bindings exist in each.
2. Specify hygiene using a concrete scope model; “hygienic” alone is not enough.
3. Specify deterministic expansion step, memory, recursion, and output limits.
4. Forbid compile-time I/O and separate external build tasks from macros.
5. Forbid user changes to tokenization, arbitrary grammar, type rules, optimizer
   passes, and lowering.
6. Require definition-site and call-site provenance on generated syntax.
7. Define whether deliberate capture exists; safest v1 choice is standard-library
   only.
8. Forbid same-module syntax generation that changes later expansion behavior.
9. Define canonical expansion serialization so tools and AI agents see the same
   representation.
10. Count the primitive catalog, runtime semantics, and macro evaluator in the
    whole-language documentation budget.

### Reconsider or prototype before committing

- **Floating point is now a parameterized Core family:** the earlier opaque
  library direction was rejected because it impedes optimization, constant
  folding, portability, debugging, and ABI consistency. Core provides
  `Float[Format, Behavior]`; the standard library supplies F16, BF16, F32, and
  F64 definitions with a finite set of validated behaviors. This complete
  mechanism counts toward the Core primitive catalog and documentation budget.
- **Privileged standard macros emitting assembly:** provenance-based privilege is
  feasible, but it expands the trusted computing base. Prefer standard macros
  that call typed trusted primitives unless direct emission yields a measured
  benefit.
- **User-defined symbolic notation:** allow only the restricted infix scheme until
  parser performance, formatter behavior, and diagnostic quality are proven.
- **Typeclass inference:** retain only if a compact coherent rule set covers real
  use cases without the inference and instance-selection complexity found in
  modern Haskell.

## Suggested Acceptance Tests

The language should not call the extension design complete until it can pass
these tests:

1. A formatter and syntax highlighter can parse every file without executing
   macros or loading package code.
2. Renaming a local variable cannot change the meaning of an imported macro.
3. Every macro expansion terminates within specified resource bounds or returns
   the same deterministic error on every conforming implementation.
4. A diagnostic in generated code identifies the authored call and offers the
   relevant expansion trace.
5. Removing an unused syntax import cannot change unrelated parsing or name
   resolution.
6. Two independently authored syntax packages cannot silently change one
   another's binding behavior.
7. Canonical full expansion contains only standard surface forms and lowers to
   the same typed Core on conforming compilers.
8. Core validation rejects malformed compiler output independently of the
   surface type checker.
9. The complete normative language, primitive catalog, macro rules, and runtime
   rules fit the declared AI context budget.
10. Representative projects remain readable after dependencies are unavailable
    because their expanded interface and syntax dependencies are recorded.

## Bottom Line

The best research-supported design is not “a language users can redefine.” It is
“a small fixed language in which users can define concise, hygienic ways to write
ordinary programs.”

GHC supplies the typed semantic center and validation model. Scheme/Racket
supply hygiene and syntax provenance. Lisp supplies structural abstraction and
domain vocabulary. Forth supplies the ideal of a small inspectable system.
Rust, Template Haskell, grammar-extension systems, and language workbenches
mainly supply boundary warnings: avoid ambient compile-time power, arbitrary
grammar, compiler-internal APIs, and extension mechanisms larger than the
language they extend.

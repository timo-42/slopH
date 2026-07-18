# Small Typed Core

This document records the proposed architecture for a small, explicitly typed
canonical Core language. Surface syntax, standard syntax sugar, and user macros
must elaborate into this Core before optimization and native code generation.

The examples and Core forms below are provisional design directions rather
than a finalized grammar.

## Inspiration: GHC Core

GHC translates Haskell into a small, explicitly typed intermediate language
after type checking and desugaring. Complex surface features can then be
represented using a small number of expression forms.

Modern GHC Core has ten `Expr` constructors:

1. `Var` for local and global references.
2. `Lit` for primitive literals.
3. `App` for term, type, and coercion application.
4. `Lam` for term and type abstraction.
5. `Let` for recursive and non-recursive bindings.
6. `Case` for evaluation and pattern branching.
7. `Cast` for changing an expression's type using a coercion.
8. `Tick` for profiling, coverage, breakpoints, and source annotations.
9. `Type` for types used as expression arguments.
10. `Coercion` for explicit proofs of type equality.

The current definition is visible in the
[GHC Core source][ghc-core-source].

Typeclasses can be represented as explicit dictionary values and function
arguments. Surface constructs such as guards, list comprehensions, and `do`
notation desugar into ordinary Core expressions and library calls.

## Why This Fits the Project

The proposed compilation pipeline is:

```text
Extensible source syntax
        |
        | macro expansion
        v
Standard surface language
        |
        | type checking and elaboration
        v
Explicitly typed Core
        |
        | Core validation and optimization
        v
Machine/backend IR
        |
        v
Native executable
```

This architecture provides the following properties:

- every macro must eventually reduce to ordinary Core;
- operators reduce to typeclass operations;
- typeclasses can reduce to dictionaries and ordinary functions;
- loops, assertions, range types, and numeric libraries do not require new
  Core syntax merely for convenience;
- compiler optimizations only need to understand the Core;
- the compiler can expose a canonical representation for AI inspection;
- a Core validator can check transformations after elaboration and
  optimization passes.

This separation permits an expressive surface language without allowing each
surface feature to enlarge the semantic kernel.

## The Important Size Distinction

A small number of expression constructors does not by itself make a small
language. Much of GHC Core's complexity lives outside its constructor list:

- the grammar and equality rules for types and kinds;
- primitive operations and primitive representations;
- polymorphism and type application;
- coercions and their proof rules;
- evaluation order and laziness;
- pattern alternatives and recursive bindings;
- lifted and unlifted values;
- calling conventions and runtime representation;
- runtime allocation and garbage collection.

GHC's type representation, for example, supports kinds, polymorphism,
multiplicities, predicates, and several runtime-representation distinctions.
See the [GHC Core type documentation][ghc-core-types].

The relevant size metric for this project is therefore:

> Core syntax + Core type system + operational semantics + complete primitive
> catalog.

That complete package must fit within the intended AI context budget. Counting
only AST constructors would hide the majority of the semantic surface.

## Proposed Initial Core

The initial Core should aim for approximately the following forms:

```text
Var    local or global reference
Lit    exact kernel literal
App    term or type application
Lam    term or type abstraction
Let    recursive or non-recursive binding
Case   evaluation and pattern branching
Type   explicit type argument
Prim   application of a cataloged kernel primitive
Con    construction of a resolved nominal enum or struct alternative
```

This list is intentionally provisional. Each form is admitted only if it
cannot be expressed cleanly and efficiently using the others.

### Variables and Applications

`Var`, `App`, and `Lam` form the ordinary explicitly typed functional core.
Type abstraction and type application may share `Lam` and `App` with term
abstraction and application, provided binders and arguments carry an explicit
category.

Names in Core are already resolved identifiers. Surface name lookup, imports,
macro hygiene, and operator resolution are complete before Core is emitted.

### Modules and Global Identities

Modules or namespaces are required at the source-language and compilation
boundary, but they are not Core expression forms. Module loading, import
resolution, visibility checking, and selection between qualified names happen
before Core is emitted.

A Core global reference uses a stable, fully resolved identity conceptually
equivalent to:

```text
package-id :: module-path :: declaration-id
```

This notation is illustrative rather than final syntax. It means that a Core
reference never depends on the importing module's aliases, wildcard imports,
source spelling, or search order.

The exact source module design remains deferred, including:

- how module names map to files and directories;
- import, qualification, alias, export, and re-export syntax;
- public and private visibility;
- whether types, values, constructors, syntax, and operators share namespaces;
- whether dependency cycles are permitted;
- package identity, versions, and dependency selection;
- the contents and compatibility rules of compiled module interfaces.

The eventual design must preserve separate compilation and deterministic Core
output. It must also support the explicit compile-time syntax dependencies
defined in [MACRO.md](./MACRO.md).

### Bindings

`Let` distinguishes recursive from non-recursive binding groups. The Core
specification must define evaluation order and whether a binding denotes a
value, delayed computation, mutable location, or another runtime object.

This project should not inherit Haskell's lazy semantics accidentally. The
evaluation strategy must be explicit and small.

### Case Analysis

`Case` is the sole general branching and pattern-analysis form in Core. Surface
conditionals, matches, guards, and other control-flow sugar elaborate into
`Case` plus ordinary functions or primitives.

Core alternatives must use already-resolved constructors and explicit binders.
Exhaustiveness and unreachable-pattern diagnostics belong to surface type
checking; the Core validator checks structural and type correctness.

For a closed enum, Core alternatives are exhaustive and contain one resolved
constructor plus simple field binders. Surface nested patterns, guards, and
wildcards lower into nested exhaustive `Case` expressions.

### Nominal Construction

`Con` constructs a resolved alternative of a nominal enum or struct type. The
constructor identity, generic type arguments, field order, and field types are
explicit enough for independent validation.

Enums, structs, tuples, `Unit`, `Option`, `Result`, and the standard `Bool` type
all use the same `Con` and `Case` mechanism. Structs are single-constructor
enums; `Unit` is a single constructor with no fields. See
[ENUM.md](./ENUM.md).

Core optimization is constructor-driven rather than `Bool`-name-driven. Case of
a known constructor, redundant tag tests, construct-then-destructure sequences,
and compact fieldless-enum layout are general rules. A two-constructor fieldless
enum therefore receives the same baseline optimizations as standard `Bool`
without acquiring Boolean truthiness or operators. The canonical standard
`Bool` identity exists for primitive signatures and standard surface typing, not
as an optimizer privilege.

Irreducible comparisons over `Int`, floats, addresses, and other primitive
domains are cataloged Core primitives returning the standard nominal `Bool`.
Boolean `not`, equality, xor, and related logic remain standard-library
`Con`/`Case` definitions. Short-circuiting `and` and `or` are surface
transformations into `Case`. Core does not add `bool_not`, `bool_and`, `bool_or`,
or `bool_xor` primitives.

### Types

Core is explicitly typed. Every binder and expression has enough information
for an independent validator to reconstruct or verify its type.

The initial design should avoid explicit `Cast` and `Coercion` forms unless the
chosen type system genuinely requires representational type equality, GADTs,
type families, or related features. Omitting those features would make this
Core substantially smaller than System FC.

Source positions, profiling annotations, and expansion provenance should be
metadata rather than semantic `Tick` expressions unless later evidence shows
that transformations need them inside the expression grammar.

## Primitive Catalog

`Prim` represents behavior that cannot be implemented as ordinary Core
functions. The complete primitive catalog is part of the language's semantic
size and must remain tightly controlled.

Likely primitive categories include:

- unbounded `Int` values, exact integer operations, and bounded-range policy
  operations;
- fixed-width bit operations used during representation lowering;
- the parameterized `Float[Format, Behavior]` family and its cataloged
  floating-point operations;
- allocation and deterministic destruction;
- low-level memory access required by the selected ownership model;
- traps or unrecoverable termination;
- external function calls;
- the trusted standard-library assembly interface;
- target and runtime operations that cannot be expressed portably in Core.

Library functions should implement everything that does not require privileged
compiler or machine behavior. A convenience operation does not become a
primitive merely because implementing it as a library function takes more
code.

Every primitive needs:

- a complete type;
- operational semantics;
- ownership and effect behavior;
- trapping or failure behavior;
- target availability;
- a native-lowering rule;
- validator rules.

The primitive catalog should be reviewable as one compact document or table.

## Ownership in Core

Ownership information must not be erased before transformations that could
duplicate, discard, or reorder owned values.

Two lowering strategies remain possible:

1. Retain ownership, consumption, and borrowing information in Core types and
   validate it after every relevant transformation.
2. Complete ownership checking before Core optimization and lower ownership
   into explicit moves, drops, and memory operations that ordinary effect-aware
   optimizations must preserve.

The chosen strategy must prevent an optimizer from:

- using a value after it has moved;
- duplicating a uniquely owned value;
- removing a required destruction or resource release;
- extending a temporary borrow beyond its permitted call;
- reordering mutation across an exclusive borrow;
- treating a trapping or effectful primitive as pure.

Because the current memory-model direction uses unique ownership and
non-storable call-scoped borrows, the Core should not need Rust-style lifetime
parameters. It still needs enough information to preserve consumption and
effects correctly.

## Core Validation

The compiler should include a Core validator analogous in purpose to GHC's
Core Lint. GHC's published external Core description explains that Core Lint
can type-check desugared programs and verify compiler transformations; see the
[external Core documentation][ghc-external-core].

The validator should check at least:

- all referenced identifiers are in scope;
- binder and expression types are well formed;
- applications match function parameter types;
- `Case` alternatives agree on their result type;
- constructor alternatives have correct fields;
- constructor applications name valid alternatives with correct type arguments
  and fields;
- recursive groups satisfy their binding restrictions;
- primitive calls use valid types and effects;
- ownership and consumption invariants hold at the applicable Core phase;
- trusted operations carry valid, non-forgeable provenance.

Validation should be available in development and test builds after every Core
transformation. A cheaper mandatory validation pass may run in normal builds.

## Canonical Core Output

The compiler should provide a stable, deterministic textual representation of
Core for:

- AI inspection;
- compiler debugging;
- macro auditing;
- optimization tests;
- reproducible snapshots;
- explaining type and ownership errors after elaboration.

The eventual CLI should provide an operation equivalent to `--emit-core`.
Canonical output should use resolved names, explicit types, fixed formatting,
and no dependence on hash-map or import traversal order.

If the textual Core is made a public interchange or AI contract, it must be
versioned. Internal compiler data structures may evolve independently as long
as they serialize to the documented versioned form.

## Relationship to Macros

The extensibility model in [MACRO.md](./MACRO.md) should elaborate through this
Core boundary:

- leading-name syntax macros expand before ordinary type checking;
- generated declarations are type-checked like handwritten declarations;
- custom operators become typeclass method calls;
- macro expansion provenance is retained as Core metadata;
- canonical expansion output may show surface expansion, typed Core, or both;
- macros cannot introduce new Core forms or primitives.

The mandatory standard syntax profile is therefore a library-defined surface
over a fixed Core rather than a second semantic language.

## Relationship to Typeclasses

Homogeneous operator protocols can elaborate into explicit ordinary calls.

If dictionary passing is selected, an operator such as:

```text
x + y
```

can elaborate conceptually into:

```text
Add.add(add_dictionary_for_T, x, y)
```

The dictionary itself is an ordinary Core value containing functions. No
special Core form is required.

If monomorphization is selected instead, specialization happens during or
after elaboration and still produces ordinary applications and functions.
Dictionary passing versus monomorphization remains a compiler and ABI decision,
not a new Core syntax decision.

## Parameterized Core Floating Point

Core has one primitive floating-point family:

```text
Float[Format, Behavior]
```

The mandatory standard library declares the supported `Binary16`, `BFloat16`,
`Binary32`, and `Binary64` format metadata. It instantiates each with one of four
validated explicit behavior combinations: IEEE, saturating, trapping, or
checked. Plain `Float` without a format and behavior is not a value type.

Literals, arithmetic, comparison, conversion, classification, and bit-conversion
operations belong to the parameterized primitive catalog. Surface operators may
still elaborate through typeclass operations, but ultimately call typed Core
float primitives. Checked types instead expose named operations returning
`Result` so the homogeneous operator protocol remains small.

Targets use native instructions when their behavior conforms and a deterministic
software implementation otherwise. Precision, rounding, subnormal behavior,
canonical NaNs, exceptions, and compile-time/runtime consistency are language
semantics rather than backend choices. See [FLOAT.md](./FLOAT.md).

Fixed-width integers are user-definable bounded nominal types over the one
unbounded Core `Int`, not separate Core primitives. Their declared bounds and
overflow policies permit mandatory compact lowering to fixed-width operations
and storage. Constant construction can be checked during compilation; runtime
construction and checked arithmetic use explicit `Result` values. See
[INTEGER.md](./INTEGER.md) for the normative design direction.

## Optimization Direction

A small Core makes optimization easier to audit but does not require an
aggressive optimizer. Fast compilation remains a top-level goal.

Initial passes should be few, deterministic, and independently validated. Good
early candidates include:

- substitution and dead non-effectful binding elimination;
- constant evaluation of documented pure primitives;
- simplification of known `Case` alternatives;
- small-function inlining under a strict size budget;
- typeclass dictionary simplification if dictionary passing is selected;
- explicit lowering of ownership and destruction;
- direct lowering to the chosen backend IR.

Advanced whole-program specialization, repeated simplifier fixed points, and
large search-based optimizations should not be prerequisites for acceptable
runtime behavior.

## Recommendation

The small typed Core should become the normative semantic definition of the
language. Surface syntax and standard-library abstractions may evolve, but they
must elaborate into this fixed semantic foundation.

The Core is small only if all of the following stay small together:

- expression forms;
- type and ownership rules;
- effect semantics;
- primitive operations;
- standard evaluation behavior.

This provides a better guard against language growth than counting source
features or AST constructors alone.

## Open Decisions

- The exact Core grammar and textual syntax.
- Strict versus lazy evaluation behavior for bindings.
- The final type and kind system.
- Whether types share the term grammar or use a separate grammar.
- The ownership-preserving versus explicit-drop lowering point.
- The complete primitive catalog.
- Dictionary passing versus monomorphization.
- The versioning and compatibility policy for public Core text.
- The initial optimization pipeline and budgets.
- The backend IR and native code-generation path.
- The source module, namespace, package, and compiled-interface rules.

## References

- [Current GHC Core expression definition][ghc-core-source]
- [GHC Core type representation][ghc-core-types]
- [External representation of GHC Core][ghc-external-core]
- [GHC rebindable syntax and Core Lint][ghc-rebindable]

[ghc-core-source]: https://ghc.gitlab.haskell.org/ghc/doc/libraries/ghc-9.15-inplace/src/GHC.Core.html
[ghc-core-types]: https://downloads.haskell.org/~ghc/9.12.3/docs/libraries/ghc-9.12.3-40be/GHC-Core-Type.html
[ghc-external-core]: https://downloads.haskell.org/~ghc/7.6.3/docs/html/users_guide/an-external-representation-for-the-ghc-core-language-for-ghc-6.10.html
[ghc-rebindable]: https://ghc.gitlab.haskell.org/ghc/doc/users_guide/exts/rebindable_syntax.html

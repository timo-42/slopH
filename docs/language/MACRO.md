# Macros and Extensibility

This document records the current design direction for user-defined syntax,
compile-time execution, operators, and trusted low-level standard-library
facilities.

The examples use provisional syntax. They demonstrate semantics and are not a
final grammar.

## Goals

The extensibility system should:

- keep the semantic kernel small;
- let libraries express conveniences in terms of existing kernel operations;
- let AI systems recognize and inspect every extension;
- preserve predictable parsing and fast compilation;
- support readable, domain-specific notation without silently changing the
  meaning of unrelated code;
- make generated code and low-level behavior auditable.

Easy manual authoring remains a non-goal. Syntax sugar is valuable when it
improves human readability or reduces the amount of code and documentation an
AI must process.

## Terminology

A **primitive** introduces behavior that cannot be expressed using existing
language behavior. Primitives belong to the semantic kernel or to a trusted
target interface.

A **syntax macro** introduces a new way to write behavior that already exists.
It expands into ordinary syntax and does not add semantics to the language.

An **operator** is an infix spelling for a normal typeclass operation.

A **build task** is a separate program that may access external state and
produce declared build inputs. It is not a syntax macro.

User libraries may define syntax, types, functions, typeclass instances, and
operators. Initially, only the trusted standard library may use the assembly
primitive.

### Possible Public Name: Sugar or Transformation

`macro` is the current technical term, but it does not have to be the public
name of this feature. We may instead call it **syntax sugar**, **sugar**, or a
**syntax transformation**.

This naming would emphasize the intended Haskell-like model: convenient surface
forms are translated into already existing, smaller language forms. A user
transformation changes how a program is written, not what the language can
ultimately mean. It cannot add a new primitive, type rule, evaluation rule, or
Core construct.

Possible spellings include:

```text
sugar unless($condition: Expr, $body: Block)
    => quote { if not($condition) $body }

transform unless($condition: Expr, $body: Block)
    => quote { if not($condition) $body }
```

**Sugar** is shorter and clearly communicates that no new semantics are added.
**Transformation** is more precise for larger rewrites and may be clearer to AI
systems because it names the operation directly. The implementation may still
use `macro` as an internal compiler term regardless of the final surface name.

The final name and keyword remain undecided. Until then, this document uses
“macro” for consistency.

## Language Layers

The language has two universally available layers:

1. A small semantic kernel containing the operations needed to define and run
   programs.
2. A mandatory standard syntax profile implemented using the kernel's macro
   facilities.

Every implementation must ship the same standard profile. An AI may assume
that profile without separately loading project-specific syntax definitions.

The mandatory `sloph` package supplies nominal `Bool`, `Unit`, `Option`, and
`Result` definitions, while `prelude` re-exports their type names. `Bool` is an
ordinary two-constructor enum;
surface conditionals and short-circuit Boolean forms transform into exhaustive
Core `Case` expressions. Boolean functions use ordinary `Con` and `Case` rather
than Bool-specific primitives; only irreducible comparisons remain cataloged
Core primitives returning standard `Bool`. See [ENUM.md](./ENUM.md).

Additional syntax belongs to explicitly imported libraries. The effective
language of a module is therefore the kernel, the standard profile, and the
syntax imports visible in that module.

The concrete module and namespace system remains undecided. Whatever design is
selected must preserve the distinction between ordinary run-time dependencies
and compile-time syntax dependencies described below. Modules are resolved
before Core is emitted; they do not introduce new Core expression forms.

## Leading-Name Syntax Macros

Every v1 bare syntax invocation begins with the name of its macro. The parser
uses that name to select exactly one imported macro before parsing the rest of
the invocation.

Illustrative declaration and use:

```text
syntax unless($condition: Expr, $body: Block)
    => quote { if not($condition) $body }

unless file.exists {
    create(file)
}
```

The leading name provides the following properties:

- parser dispatch does not require trying every imported syntax pattern;
- importing an unrelated macro cannot reinterpret an ordinary expression;
- macro uses are searchable even without a punctuation marker such as `!`;
- ambiguity is detected at the macro name rather than after speculative
  parsing.

A macro may use literal words and punctuation after its leading name, provided
its declaration describes them using existing syntactic categories.

Illustrative example:

```text
syntax repeat($count: Expr, "times", $body: Block)
    => quote { loop from 0 to $count $body }

repeat 4 times {
    send_ping()
}
```

### Imports

Project-defined macros and operators require explicit syntax imports. Syntax
imports are separate from value and type imports and are never transitive.

Illustrative example:

```text
import data.range
import syntax data.range.{range}
import operator math.vector.{<+>}
```

A module cannot acquire new grammar merely because one of its dependencies
imports or re-exports another syntax package.

Conflicting imports of the same macro name or operator symbol are compile-time
errors. The compiler does not select one based on import order.

### Inputs and Outputs

Macro patterns capture existing syntax categories such as names, expressions,
types, blocks, statements, and declarations. Captures are syntax values rather
than raw pointers into compiler internals.

V1 macros may generate:

- expressions;
- statements and blocks;
- functions;
- types;
- typeclass instances;
- other ordinary declarations.

Macros do not initially receive resolved compiler types or perform typed
reflection. Expansion happens using syntax and compile-time values available
before ordinary type checking.

Macros cannot generate macro declarations, syntax imports, operator fixities,
or build-task declarations. Those constructs affect how the rest of a module
is parsed and must be available before expansion begins.

### Quote and Splice

Macros construct syntax through hygienic quotation and splicing rather than by
mutating the compiler's internal AST.

Illustrative example:

```text
syntax assert($condition: Expr)
    => quote {
        if not($condition) {
            panic("assertion failed")
        }
    }
```

Quotation produces a stable, language-defined syntax representation. Splicing
inserts captured or constructed syntax of a compatible category.

### Hygiene

Macro expansion is hygienic by default:

- names introduced by a macro cannot accidentally capture names at the call
  site;
- call-site names referenced through captures retain their original binding;
- private names used by a macro remain resolved in the macro's defining
  module;
- generated declarations report their macro provenance.

Any future facility for intentional capture must be explicit and auditable. It
is not part of v1.

### Purity and Limits

Normal syntax macros are deterministic and have no filesystem, environment,
process, clock, randomness, or network access.

Macro execution is subject to compiler-defined time, memory, recursion, and
expansion-size limits. Exceeding a limit is a compile-time error with an
expansion trace. These limits prevent a macro from hanging compilation or
expanding a small source file into unbounded code.

The compiler may cache a macro expansion using the macro implementation,
invocation syntax, compile-time arguments, target description, and standard
profile version as inputs.

## Future Mixfix Syntax

General pattern competition is more expressive than leading-name dispatch. It
could support suffix or mixfix forms such as:

```text
send_alert() unless user.is_admin
```

It also requires rules for overlapping patterns, longest matches, precedence,
backtracking, import conflicts, and parser resource limits. Importing a library
could change the parse of an existing expression.

V1 intentionally implements only the leading-name subset. General pattern
competition may be investigated later. V1 makes no compatibility promise for
that future extension.

## User-Defined Operators

V1 permits user-defined ASCII infix operators. Prefix, postfix, Unicode, and
arbitrary mixfix operators are excluded.

An operator declaration specifies:

- an ASCII punctuation sequence;
- left, right, or non-associativity;
- a precedence from a small language-defined numeric range;
- the typeclass method to which the operator desugars.

Illustrative example:

```text
operator infixl 6 <+> using Combine.combine
```

An operator does not introduce special evaluation rules. Short-circuiting,
binding, control flow, or selective evaluation requires a syntax macro instead
of an operator.

Built-in and standard-profile symbols follow the same fixity model. A symbol
has one fixity wherever it is visible. Importing two incompatible declarations
for the same symbol is an error.

An operator symbol resolves to exactly one visible declaration before operand
type checking. That declaration names exactly one protocol method. Resolution
does not rank overload candidates, perform argument-dependent lookup, or search
chains of implicit conversions; an absent or conflicting declaration is a
compile-time error.

## Operator Typeclasses

Operators dispatch through typeclass protocols rather than compiler-owned
numeric types.

Initial arithmetic protocols are homogeneous: both operands and the result
have the same type.

Illustrative interface:

```text
class Add[T] {
    fn add(left: T, right: T) -> T
}

operator infixl 6 + using Add.add
```

A type supplies an instance backed by an ordinary function or Core primitive:

```text
instance Add[IEEE_F32] {
    fn add(left: IEEE_F32, right: IEEE_F32) -> IEEE_F32 {
        prim.float_add[Binary32, Ieee](left, right)
    }
}
```

Mixed operand types and operations whose result has another type use explicit
named functions in v1. This avoids associated output types, multi-parameter
classes, and implicit numeric conversions.

Instance coherence must ensure that a program has at most one applicable
instance for a type and typeclass. The exact instance ownership rule will be
settled with the module and typeclass design.

Generic typeclass calls use dictionary or witness passing with the uniform
representation by default. A release build may selectively monomorphize a
bounded set of calls after validated typed Core, but this is an optional cached
optimization and does not change the source semantics described here.

## Explicit Literal Macros

Numeric types not included in Core use visible macro constructors rather than
expected-type literal conversion.

Illustrative example:

```text
let x: Decimal128 = decimal128 1.25
let y: Decimal128 = decimal128 2.0
let z = x + y
```

The macro receives the exact source spelling of the literal and returns an
ordinary expression constructing the library-defined type. Invalid constant
spellings or out-of-range constants are compile-time errors.

This design makes numeric interpretation explicit and does not require the
compiler to know every library-defined numeric format.

## Compile-Time Code and Build Tasks

Pure compile-time code performs syntax expansion, constant calculation, and
validation during normal compilation. It follows the same external-effect
restrictions as syntax macros.

Tasks that require external access are separate build tasks. A build task may
access the filesystem, environment, processes, or network according to its
declared capabilities. It must declare relevant inputs and outputs so the build
system can determine dependencies, caching, and reproducibility.

Build tasks run before affected source modules are compiled. Generated files
belong to the build output area and do not silently modify tracked source
files. A build task cannot modify the parser or inject syntax imports into a
module.

Build-task execution is a host operation. Normal compile-time evaluation uses
target-independent semantics and cannot execute target assembly while cross
compiling.

## Expansion and Diagnostics

Every macro expansion is inspectable.

The compiler must provide a canonical expansion mode that shows a module after
syntax expansion using ordinary kernel or standard-profile syntax. The exact
command-line spelling will be chosen with the compiler CLI.

Diagnostics originating in generated code include:

- the location of the macro invocation;
- the macro definition and expansion stack;
- the relevant generated code;
- the original captured syntax when applicable.

Source maps preserve stepping and debugging locations across expansion.
Formatting and language-server tools obtain macro patterns and expansion
interfaces from compiled module metadata rather than executing arbitrary
external code.

## Trusted Standard-Library Assembly

The kernel provides one typed `unsafe asm` expression. V1 restricts its use to
trusted standard-library modules.

An assembly expression declares:

- the target architecture and required target features;
- input, output, and input-output operands;
- permitted register classes;
- clobbered registers and flags;
- whether it reads or writes memory;
- whether it has externally visible side effects;
- stack and control-flow behavior required by the backend.

The backend validates constraints it understands and rejects assembly on an
unsupported target. The standard library is responsible for the semantic
correctness of instruction text, constraints, and effect declarations.

An ordinary user macro cannot emit usable assembly by quoting its spelling.
Assembly authority is a non-forgeable capability attached to the trusted
standard module that defines the expansion. Provenance survives macro
expansion; copying expanded text does not copy the capability.

The standard library must supply an implementation for every target advertised
as supported by the compiler. A compiler target is incomplete until its
standard assembly abstractions and their conformance tests pass.

## Parameterized Core Floating Point

Core provides one `Float[Format, Behavior]` primitive family. The standard
profile declaratively defines F16, BF16, F32, and F64 format families using the
four validated IEEE, saturating, trapping, and checked behavior combinations.
Ordinary users may define bounded nominal types over these formats but cannot
define new bit formats in v1. The full design is recorded in
[FLOAT.md](./FLOAT.md).

The standard profile defines operator instances and named convenience functions
using typed parameterized Core primitives:

```text
instance Add[IEEE_F32] {
    fn add(left: IEEE_F32, right: IEEE_F32) -> IEEE_F32 {
        prim.float_add[Binary32, Ieee](left, right)
    }
}
```

The compiler validates the explicit option fields against the four allowed
combinations. It fixes v1 rounding to nearest-even, preserves subnormals, and
canonicalizes generated NaNs. Native instructions are used only when conforming;
otherwise the standard deterministic software fallback supplies the operation.

## User-Defined Range and Bounded Integer Types

The language supports nominal validation types whose values are restricted to
a closed range. Integer range types additionally declare an overflow policy;
the standard profile uses this same facility to define conventional signed and
unsigned machine-width families. The full integer design is recorded in
[INTEGER.md](./INTEGER.md).

Illustrative example:

```text
type Die = Int 1..6 overflow error

Die(3)       // valid constant
Die(9)       // compile-time error
Die(input()) // checked conversion returning Result[Die, RangeError]
```

V1 range types are nominal validation wrappers, not flow-sensitive refinement
types:

- constant construction is checked during compilation;
- runtime construction returns an explicit `Result`;
- the representation is private, so unchecked construction is impossible in
  ordinary user code;
- wrapping, trapping, and saturating integer policies remain closed over their
  bounded type;
- an `overflow error` integer uses explicitly named checked operations returning
  `Result`, because the initial homogeneous operator protocol requires
  `T + T -> T`;
- bounded integer representation lowering is mandatory compiler behavior rather
  than an optional optimization;
- the type checker does not propagate interval bounds through expressions or
  narrow bounds after conditions.

A bounded float is a nominal type over a standard float definition rather than
a true subtype:

```text
type Percent = IEEE_F16 {
    range {
        minimum 0.0 inclusive
        maximum 100.0 inclusive
    }
    range overflow error
}
```

Bounded floats support explicit `error`, `trap`, and `saturate` range policies;
float range wrapping is excluded. Conversions to the base format are explicit.
The base operation rounds and applies its exceptional behavior before the range
policy is checked. Each endpoint may be inclusive or exclusive. NaN and infinity
never belong to a finite bounded interval; an exclusive zero endpoint excludes
both signed zeros and gives the compiler a stable nonzero fact.

Type-level interval arithmetic and flow-sensitive range inference may be
researched later. They are not required to implement safe opaque range types.

## Security and Compatibility Rules

- Macro implementations are part of a module's compile-time dependency graph.
- Expansion is deterministic unless an explicitly invoked build task produces
  a declared input.
- Syntax imports never arrive transitively.
- Macro names and operator fixities cannot be selected by import order.
- Ordinary macros cannot acquire standard-library assembly authority.
- Macro-generated public declarations appear in module interfaces with their
  expansion provenance.
- Changing an exported macro pattern, expansion contract, or operator fixity
  is an interface compatibility change.
- Tooling may inspect expansion metadata without granting external build-task
  capabilities.

## Deferred Decisions

The following decisions depend on language areas that have not yet been
designed:

- module naming, file mapping, visibility, exports, re-exports, packages, and
  namespace separation;
- the exact dictionary layout, uniform representation ABI, and release
  specialization budget for generic typeclass calls;
- the exact typeclass instance ownership and coherence rule;
- the concrete macro, quote, splice, and import syntax;
- compile-time resource-limit values;
- the compiler CLI spelling for canonical expansion;
- the first supported target architectures and features;
- the internal and foreign calling conventions;
- whether later compiler protocols expose optimizer, ABI, or debugger support
  to trusted standard-library types;
- whether a future language version supports pattern competition or other
  mixfix syntax.

These items do not change the v1 source-level direction recorded above. They
must be resolved before implementing the affected compiler or ABI subsystem.

## References

- [Rust inline assembly reference](https://doc.rust-lang.org/reference/inline-assembly.html)
- [LLVM language reference](https://www.llvm.org/docs/LangRef.html)
- [IEEE 754-2019 floating-point standard](https://standards.ieee.org/ieee/754/6210/)
- [Berkeley SoftFloat](https://qemu.googlesource.com/berkeley-softfloat-3/+/refs/heads/master/README.html)

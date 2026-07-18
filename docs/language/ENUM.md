# Enums, Structs, Unit, and Bool

This document records the current design direction for nominal algebraic data,
construction, inspection, and the standard `Bool` type. Example syntax is
provisional; the semantic model is the decision.

## Goals

The data model should:

- use one mechanism for enums, structs, tuples, unit, options, results, and
  booleans;
- keep constructor identity explicit and nominal;
- make every Core branch typed and exhaustive;
- permit efficient target-specific layouts without exposing them as semantics;
- preserve ownership and effects while constructing and destructuring values;
- avoid null, implicit numeric discriminants, structural subtyping, and special
  Boolean control-flow rules.

## Nominal Enum Declarations

An enum declares a closed set of named constructors. Each constructor has zero
or more ordered fields:

```text
type Result[T, E] {
    Ok(T)
    Err(E)
}
```

The type and every constructor have resolved global identities. Two enums with
identical constructors remain different types:

```text
type First  { Value(Int) }
type Second { Value(Int) }
```

`First.Value(1)` cannot be used as `Second`, and there is no implicit structural
conversion.

Constructor fields may have source-level names, but typed Core records their
resolved order and types. Generic constructor applications carry explicit type
arguments in canonical Core.

## Core Construction

Core has an explicit `Con` expression:

```text
Con Result.Ok[Int, Error](42)
Con Result.Err[Int, Error](error)
```

The result type follows from the constructor identity and explicit type
arguments. The Core validator checks:

- the constructor belongs to the named nominal type;
- the number, order, and types of fields match its declaration;
- all generic arguments are well formed;
- ownership of each field is valid and transferred to the constructed value;
- no private or unchecked constructor is forged.

Constructors are not disguised as primitive literals or arbitrary global
functions. The explicit form makes validation, ownership, provenance, and
representation lowering easier to audit.

## Core Inspection

`Case` is the only general Core mechanism for inspecting an enum:

```text
case result : Int {
    Result.Ok(value: Int) ->
        value

    Result.Err(error: Error) ->
        handle_error(error)
}
```

For an enum scrutinee, Core `Case` must be exhaustive and may not repeat a
constructor. Every alternative returns the declared `Case` result type.

Core alternatives contain one resolved constructor and simple field binders.
Nested patterns, guards, alternatives joined with `|`, and wildcard source
patterns are surface conveniences. They lower into nested exhaustive `Case`
expressions and ordinary functions.

The Core validator also checks that moving or borrowing a field through a
pattern follows the selected ownership model. An optimizer must not duplicate
or discard owned fields merely because their containing constructor is known.

## Bool Is an Enum

`Bool` belongs to the mandatory standard library and is semantically an ordinary
closed enum:

```text
standard module bool

type Bool {
    False
    True
}
```

The exact source spellings are provisional. The standard syntax profile may
provide `false` and `true` sugar, but canonical resolved code refers to the
standard constructors:

```text
Con std::bool::Bool.False()
Con std::bool::Bool.True()
```

`Bool` is not a separate Core primitive type and has no unique branching form.
The standard compiler environment records its stable library identity only so
primitive signatures and standard surface forms can name one unambiguous type:

```text
prim.int_equal(a, b)      -> std::bool::Bool
prim.float_less(a, b)     -> std::bool::Bool
prim.address_equal(a, b)  -> std::bool::Bool
```

This identity grants no privileged layout or optimization. User code may define
another two-constructor enum; it does not replace the standard type used by
primitive signatures or `if`, but it receives the same shape-based Core and
lowering optimizations.

## Conditionals

Surface `if` is standard sugar over `Case`:

```text
if condition {
    yes()
} else {
    no()
}
```

expands conceptually into:

```text
case condition {
    Bool.True  -> yes()
    Bool.False -> no()
}
```

Both alternatives must return the same type. There is no truthiness conversion:
integers, pointers, strings, options, and other enums cannot be used as
conditions without an explicit operation producing `Bool`.

Boolean operations are ordinary standard-library functions or operator
instances:

```text
fn not(value: Bool) -> Bool {
    case value {
        Bool.False -> Bool.True
        Bool.True  -> Bool.False
    }
}
```

Short-circuiting `and` and `or` are surface transformations into `Case`, not
eager ordinary functions and not new Core primitives.

### Standard Boolean API

The mandatory standard library supplies at least:

```text
Bool
Bool.False
Bool.True

not
equal
not_equal
xor
```

These functions are defined using ordinary `Con` and `Case`:

```text
fn equal(left: Bool, right: Bool) -> Bool {
    case left {
        Bool.False -> not(right)
        Bool.True  -> right
    }
}

fn xor(left: Bool, right: Bool) -> Bool {
    case left {
        Bool.False -> right
        Bool.True  -> not(right)
    }
}
```

The standard syntax profile provides short-circuiting forms by transformation:

```text
left and right

    => case left {
           Bool.False -> Bool.False
           Bool.True  -> right
       }

left or right

    => case left {
           Bool.False -> right
           Bool.True  -> Bool.True
       }
```

Because function application is strict, these forms must not desugar to eager
two-argument functions. The right operand is evaluated only in the required
alternative.

## Primitive Boundary

Boolean logic does not require Boolean-specific Core primitives. The following
are deliberately excluded:

```text
prim.bool_not
prim.bool_and
prim.bool_or
prim.bool_xor
```

They are expressible with `Con` and `Case`, and generic enum simplification can
lower them efficiently.

Operations that inspect an irreducible Core or machine value remain cataloged
primitives. Their declared result is the mandatory standard `Bool`:

```text
prim.int_equal(a: Int, b: Int) -> Bool
prim.int_less(a: Int, b: Int)  -> Bool

prim.float_equal[F](a: F, b: F) -> Bool
prim.float_less[F](a: F, b: F)  -> Bool

prim.address_equal[T](a: Address[T], b: Address[T]) -> Bool
```

The standard library builds comparison protocols and operators around these
irreducible leaves. For example, surface `a == b` selects an `Equal[T]` library
operation whose standard integer instance calls `prim.int_equal`.

A primitive returning `Bool` does not make `Bool` a Core primitive type. It
means only that the primitive's cataloged result is one of the two constructors
of a stable standard nominal type. The compiler validates that primitive results
cannot create any other `Bool` state.

A user-defined two-constructor enum may map a primitive result without permanent
materialization:

```text
type Answer { No Yes }

fn compare(a: Int, b: Int) -> Answer {
    case prim.int_equal(a, b) {
        Bool.False -> Answer.No
        Bool.True  -> Answer.Yes
    }
}
```

Generic case and representation lowering can turn this directly into a machine
comparison producing the `Answer` tag. No intermediate runtime `Bool` value is
required.

## Uniform Enum Optimization

The optimizer does not recognize `Bool` by name for ordinary data-flow
optimization. It recognizes general `Con` and `Case` patterns that apply to all
closed enums:

```text
case Con C(fields) { ... C(bindings) -> body ... }
    -> body with fields substituted for bindings

case value { C1 -> Con C1; C2 -> Con C2 }
    -> value
```

It may also eliminate redundant tag tests, combine nested cases, remove
construct-then-destructure pairs, and select compact representations from the
number of constructors and their payloads.

Consequently, every fieldless two-constructor enum receives the same fundamental
representation and branch optimizations as `Bool`:

```text
type Switch { Off On }
type OrderingChoice { Lower Higher }
```

An enum with two constructors is not necessarily Boolean. Constructors may have
payloads, and the type has no automatic truthiness, `and`, `or`, or `not`
semantics. Only its shared enum structure is optimized generically. A custom
fieldless two-constructor enum inspected with `Case` should generate code of the
same quality as an equivalent `Bool` branch.

Boolean algebra simplification is obtained by expanding or inlining the
standard `Bool` functions into ordinary `Con` and `Case` forms. A special
Bool-only optimizer pass should not be required for correctness or baseline
performance.

## Structs Are Single-Constructor Enums

A struct is a nominal type with exactly one constructor:

```text
type Point {
    Point(x: IEEE_F32, y: IEEE_F32)
}
```

Core construction is ordinary `Con`:

```text
Con Point(x, y)
```

Core field projection is ordinary exhaustive `Case`:

```text
case point {
    Point(x, ignored_y) -> x
}
```

Surface field access may lower to this form. The optimizer can replace a known
single-constructor case with a direct field projection during representation
lowering. Core does not need separate struct-construction, field-access, or
destructuring expressions.

## Unit and Tuples

`Unit` is a standard one-constructor enum with no fields:

```text
type Unit {
    Unit
}
```

It has exactly one semantic value and may require no runtime storage.

Tuple types are standard single-constructor generic types:

```text
type Tuple2[A, B] {
    Tuple2(A, B)
}
```

The language may offer tuple syntax as standard sugar, but Core uses the same
nominal constructor and `Case` rules.

## Option and Result

Absence and recoverable failure use ordinary standard enums:

```text
type Option[T] {
    None
    Some(T)
}

type Result[T, E] {
    Ok(T)
    Err(E)
}
```

There is no semantic `null` value and no exception-specific Core expression.
Propagation syntax such as a future `try` transformation expands into exhaustive
`Case` expressions over `Result`.

## Representation Lowering

Constructor tags, field offsets, padding, niche use, and calling conventions are
lower-IR decisions rather than Core semantics.

The backend chooses representation from enum shape rather than a special
`Bool`-only rule. Any fieldless two-constructor enum can use one logical bit in
registers and an addressable byte in ordinary memory. `Bool` naturally receives
that representation because it has exactly that shape. Only valid constructors
may enter safe typed Core; arbitrary foreign bytes require validation before
becoming the enum.

For other enums the backend may choose:

- a tag plus payload;
- an unboxed single-constructor representation;
- a niche representation when a field has invalid machine states;
- no storage for a fieldless single-value type.

These choices must preserve public ABI rules, stable foreign declarations, and
observable behavior. Safe code cannot inspect or manufacture a numeric
constructor tag unless an explicit representation/FFI facility is designed
later.

## Separate Compilation and Compatibility

A compiled type interface records:

- the nominal type identity and generic parameters;
- constructor identities, order, visibility, and field types;
- ownership properties needed by clients;
- public representation commitments, if any are explicitly selected later.

Adding, removing, or changing a public constructor or field is an interface
compatibility change. Normal enums are closed: clients may compile exhaustive
cases against the versioned interface they import.

## Relationship to Sugar

Standard or user transformations may generate enum declarations, constructors,
and surface matches. Their expanded output passes through ordinary name
resolution, visibility checks, type checking, and `Con`/`Case` elaboration.

Transformations cannot add new data semantics, forge constructors, declare a
type open after compilation, or bypass exhaustive Core validation.

## Design Boundary

The complete semantic mechanism is:

```text
declare a closed nominal type and its constructors
construct a value with Con
inspect a value with exhaustive Case
```

From this mechanism the standard library defines:

```text
Bool
Unit
Tuple
Option
Result
ordinary structs and enums
```

Only layout and machine representation require a separate lower-IR model.

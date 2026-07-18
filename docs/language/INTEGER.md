# Integer and Bounded-Integer Design

This document records the current design direction for integers, user-defined
bounded integer types, overflow policies, and native representation lowering.
The example syntax is provisional; the semantics are the decision.

## Goals

The integer system should:

- give Core one simple mathematical integer type;
- make bounds and overflow behavior explicit in source types;
- let users define domain-specific bounded integers;
- let the standard library define conventional machine-sized integer families;
- permit predictable native-width storage and arithmetic;
- avoid implicit conversions, hidden overflow rules, and integer subtyping;
- remain easy for AI to generate and humans to audit, even if manually writing
  the full type names is inconvenient.

## One Core Integer

Core has one semantic integer primitive:

```text
Int
```

`Int` denotes an unbounded mathematical integer. Its ordinary arithmetic does
not overflow or silently lose information. The runtime may use immediate
machine words for small values and an arbitrary-precision representation for
larger values, but that representation is not visible in Core semantics.

Fixed-width integer names such as `U8` and `I32` are not separate Core
primitives. They are library-defined nominal types whose values are restricted
to a closed subset of `Int`.

Floating point uses a separate parameterized Core family because precision and
rounding affect ordinary floating operations. See [FLOAT.md](./FLOAT.md).

## User-Defined Bounded Integers

A user may define a nominal integer type by specifying inclusive minimum and
maximum values plus an overflow policy:

```text
type ClockHour = Int 0..23 overflow wrap

type Percentage = Int 0..100 overflow error

type AudioLevel = Int -32768..32767 overflow saturate
```

The bounds must be compile-time `Int` constants and the minimum must not exceed
the maximum. A bounded integer declaration creates a distinct nominal type; two
declarations with identical bounds and policy are not implicitly interchangeable.

Bounded integers are not subtypes of `Int`. Conversion between `Int` and a
bounded integer is explicit. This avoids subtyping, implicit narrowing, and
context-dependent arithmetic.

## Overflow Policies

V1 defines four named policies.

### `wrap`

Arithmetic that leaves the declared range wraps modulo the number of values in
the range:

```text
wrap(value, minimum, maximum) =
    minimum
    + modulo(value - minimum, maximum - minimum + 1)
```

`modulo` returns a non-negative remainder smaller than the range size.

Examples:

```text
type WrappingU8 = Int 0..255 overflow wrap

WrappingU8(255) + WrappingU8(1) == WrappingU8(0)
WrappingU8(0)   - WrappingU8(1) == WrappingU8(255)
```

Wrapping is defined for arbitrary ranges, not only powers of two:

```text
type ClockHour = Int 0..23 overflow wrap

ClockHour(23) + ClockHour(2) == ClockHour(1)
```

### `trap`

Arithmetic returns the bounded type when the exact mathematical result is
inside the range. Otherwise execution terminates with the language's defined
integer-overflow trap.

```text
type TrappingU8 = Int 0..255 overflow trap
```

Trap behavior is identical in every build mode. It must never change between
debug and optimized builds.

### `saturate`

Arithmetic clamps an out-of-range result to the nearest bound:

```text
type SaturatingU8 = Int 0..255 overflow saturate

SaturatingU8(250) + SaturatingU8(20) == SaturatingU8(255)
SaturatingU8(3)   - SaturatingU8(10) == SaturatingU8(0)
```

### `error`

Arithmetic reports overflow as an ordinary recoverable value:

```text
type CheckedU8 = Int 0..255 overflow error

CheckedU8.checked_add(a, b) -> Result[CheckedU8, Overflow]
CheckedU8.checked_sub(a, b) -> Result[CheckedU8, Overflow]
CheckedU8.checked_mul(a, b) -> Result[CheckedU8, Overflow]
```

The initial homogeneous operator protocol requires `T + T -> T`. Therefore an
`overflow error` type does not initially provide `+`, `-`, or `*`; it provides
explicit checked named operations returning `Result`. This avoids adding
associated operator-result types solely for checked arithmetic.

The other three policies are closed over their bounded type and may implement
the ordinary homogeneous arithmetic operators.

## Construction and Conversion

The overflow policy controls arithmetic. It does not make ordinary construction
silently wrap, trap, or saturate.

Explicit construction is checked:

```text
WrappingU8(42)       // valid constant
WrappingU8(300)      // compile-time error
WrappingU8(input())  // Result[WrappingU8, RangeError]
```

Alternative conversions name their behavior:

```text
WrappingU8.wrap(value: Int)       -> WrappingU8
WrappingU8.trapping(value: Int)   -> WrappingU8
WrappingU8.saturating(value: Int) -> WrappingU8
WrappingU8.checked(value: Int)    -> Result[WrappingU8, RangeError]
```

Conversion from a bounded integer to `Int` is lossless but remains explicit:

```text
value.to_int()
```

There are no implicit conversions between bounded integer types, even when one
range contains the other. Mixed-type arithmetic requires an explicit conversion
or a named library function.

## Standard-Library Families

The mandatory standard library ships conventional definitions using the same
facility available to users:

```text
type WrappingU8   = Int 0..255 overflow wrap
type CheckedU8    = Int 0..255 overflow error
type TrappingU8   = Int 0..255 overflow trap
type SaturatingU8 = Int 0..255 overflow saturate
```

It supplies corresponding signed and unsigned families for supported widths:

```text
WrappingI8    CheckedI8    TrappingI8    SaturatingI8
WrappingU8    CheckedU8    TrappingU8    SaturatingU8
WrappingI16   CheckedI16   TrappingI16   SaturatingI16
WrappingU16   CheckedU16   TrappingU16   SaturatingU16
WrappingI32   CheckedI32   TrappingI32   SaturatingI32
WrappingU32   CheckedU32   TrappingU32   SaturatingU32
WrappingI64   CheckedI64   TrappingI64   SaturatingI64
WrappingU64   CheckedU64   TrappingU64   SaturatingU64
```

The explicit policy names are intentional. A bare `U8` or `I32` alias should
not be introduced until the language selects and documents one universal
default overflow policy.

## Typed Core Representation

A bounded integer remains a resolved nominal type in typed Core. Its type
declaration records non-forgeable integer metadata conceptually equivalent to:

```text
bounded-int {
    base: Int
    minimum: 0
    maximum: 255
    overflow: wrap
}
```

Ordinary user code cannot invoke the unchecked constructor. The Core validator
ensures that bounded values originate from a valid constant, a checked
conversion, a policy operation, or another value already carrying the type.

Core arithmetic is conceptually expressed using exact `Int` operations and an
explicit policy operation:

```text
let exact : Int = prim.int_add(reveal(a), reveal(b))
let result : WrappingU8 = prim.int_range_wrap[WrappingU8](exact)
```

This semantic form does not require a separate `U8` Core primitive.

## Mandatory Representation Lowering

Compact bounded-integer lowering is required compiler behavior, not an optional
optimization. The compiler chooses the smallest supported native storage width
that can represent the entire declared range. V1 storage widths are 8, 16, 32,
and 64 bits; a range that cannot fit uses the normal `Int` representation.

Examples:

```text
Int 0..100                 -> unsigned 8-bit storage
Int -100..100              -> signed 8-bit storage
Int 0..65535               -> unsigned 16-bit storage
Int -2147483648..2147483647 -> signed 32-bit storage
```

Registers may use a wider native width as long as observable values, memory
layout, overflow behavior, and foreign interfaces follow the specified type.

Standard power-of-two wrapping types lower directly:

```text
WrappingU8 addition -> add.i8
WrappingI32 addition -> add.i32
```

Other policies lower to native arithmetic plus the required check or selection:

```text
TrappingU8 addition   -> checked add + overflow trap
SaturatingU8 addition -> widening/flagged add + clamp
CheckedU8.checked_add -> checked add + Result construction
```

Arbitrary non-power-of-two ranges use the general range semantics. The compiler
may optimize them further, but it may not replace them with power-of-two
wrapping.

## Optimization Facts

The compiler may rely on the declared range throughout typed Core:

- a value of the type is always within its bounds;
- comparisons outside the range can be simplified;
- storage width and signedness can be selected deterministically;
- wrapping, trapping, checked, and saturating operations can use native overflow
  flags or wider intermediates;
- repeated checks can be removed when Core analysis proves the value remains in
  range.

V1 does not require general flow-sensitive range inference or type-level
interval arithmetic. Declared bounds and explicit policy operations provide the
required baseline performance. More advanced range proofs are optional
optimizations and cannot change program behavior.

## Interfaces and Compatibility

The bounds and overflow policy are part of a bounded integer's public type and
compiled module interface. Changing either is a source-compatibility and ABI
change.

Foreign values entering a bounded type must be validated unless the foreign
declaration explicitly uses its lower representation and is trusted to preserve
the invariant. Invalid storage bit patterns must never become ordinary bounded
values through safe code.

## Design Boundary

This design deliberately separates three concerns:

```text
Mathematical semantics:  Core Int
Domain semantics:        user-defined bounded nominal types and overflow policy
Machine representation:  lower-IR i8/i16/i32/i64 operations and storage
```

It keeps the semantic Core small while making integer behavior explicit and
allowing predictable native code. The verbosity of names such as
`SaturatingU16` is acceptable: it improves AI generation and human review, while
easy manual authoring remains a non-goal.

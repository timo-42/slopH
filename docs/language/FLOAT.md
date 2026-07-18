# Floating-Point and Bounded-Float Design

This document records the current design direction for floating-point formats,
exception behavior, standard types, software fallback, and user-defined bounded
float types. Example syntax is provisional; the semantic rules are the decision.

## Goals

The floating-point system should:

- use one parameterized Core floating-point family rather than unrelated
  primitive types;
- make precision, exponent range, rounding, and exceptional behavior explicit;
- allow only a small set of coherent behavior combinations;
- produce deterministic compile-time and runtime results;
- use native instructions when they implement the specified semantics;
- remain portable through a conforming software fallback;
- let users define domain-specific bounded nominal types over standard formats;
- keep behavior easy for AI to generate and humans to audit, even at the cost of
  verbose declarations and operation names.

## Why Float Is Not Exactly Like Int

Core `Int` has one exact mathematical meaning. A bounded integer can therefore
add a range and overflow rule without changing the meaning of an in-range
integer.

Floating-point operations are intrinsically format-dependent. Precision and
rounding affect ordinary results, not only exceptional overflow. There is no
practical unparameterized, exact `Float` value type that is closed under
division, square root, and transcendental operations.

Core therefore has one primitive **type family** rather than one plain float
type:

```text
Float[Format, Behavior]
```

`Float` without format metadata is not a value type.

## Format Geometry

A format describes its finite values and stable storage encoding. Conceptually:

```text
float-format Binary32 {
    radix 2
    precision 24
    exponent -126..127
    storage 32
    encoding ieee754_binary32
}
```

For a binary format:

- `precision` is the number of significant binary digits, including an implicit
  leading digit when the encoding uses one;
- `exponent` is the inclusive normal exponent range;
- subnormal values extend the finite range below the minimum normal exponent;
- `storage` and `encoding` define public layout, bit conversion, and ABI
  behavior.

Range alone cannot define a float format. Two formats may have a similar finite
range but different precision, rounding, NaN encodings, or subnormal behavior.

## Who May Define Formats

Only the mandatory standard library may declare new precision/exponent formats
in v1. Ordinary users may define behavior and bounded nominal types over those
formats, but cannot invent new bit layouts.

This restriction keeps the compiler, software reference implementation,
foreign ABI, debugger, formatter, and conformance suite finite. User-defined
binary or decimal encodings may be reconsidered later.

## Standard Formats

V1 ships four format geometries:

| Format | Radix | Precision | Normal exponent | Storage | Encoding |
|---|---:|---:|---:|---:|---|
| `Binary16` | 2 | 11 | -14..15 | 16 bits | IEEE 754 binary16 |
| `BFloat16` | 2 | 8 | -126..127 | 16 bits | bfloat16 |
| `Binary32` | 2 | 24 | -126..127 | 32 bits | IEEE 754 binary32 |
| `Binary64` | 2 | 53 | -1022..1023 | 64 bits | IEEE 754 binary64 |

The standard surface types are named:

```text
IEEE_F16
BF16
IEEE_F32
IEEE_F64
```

FP8 is deferred. There is no single universal FP8 format, and current variants
such as E4M3 and E5M2 have different exponent, precision, infinity, and NaN
rules. They may be added later as separately named standard formats.

## Explicit but Restricted Behavior

Every floating type declaration explicitly records:

```text
rounding
subnormal behavior
infinity availability
NaN behavior
overflow behavior
division-by-zero behavior
invalid-operation behavior
```

The fields are not freely combinable. V1 accepts exactly four coherent
combinations. This retains local readability without requiring the compiler to
support the full Cartesian product of policies.

| Combination | Infinity/NaN | Overflow | Nonzero / zero | Invalid operation | Result shape |
|---|---|---|---|---|---|
| IEEE | allowed/canonical | infinity | signed infinity | canonical NaN | same type |
| Saturating | forbidden | finite bound | signed finite bound | trap | same type |
| Trapping | forbidden | trap | trap | trap | same type |
| Checked | forbidden | `Overflow` | `DivideByZero` | `Invalid` | `Result` |

All four v1 combinations use:

```text
rounding nearest_even
subnormal preserve
```

The fields remain explicit in declarations even though v1 permits only these
values. Ambient process or thread rounding modes do not change language
semantics.

### IEEE combination

Conceptually:

```text
type IEEE_F32 = Float[Binary32] {
    rounding nearest_even
    subnormal preserve
    infinity allow
    nan canonical
    overflow infinity
    divide_zero infinity
    invalid nan
}
```

For division, a nonzero finite value divided by zero produces a signed infinity.
Zero divided by zero is an invalid operation and produces the canonical NaN.
Comparisons with NaN follow documented unordered IEEE behavior.

Every operation that produces NaN normalizes it to one quiet canonical NaN for
the format. The canonical value has a fixed sign and payload defined by the
format specification. Safe construction from external bits also canonicalizes
NaNs. Consequently, compile-time and runtime bit conversion are deterministic.

The language does not expose ambient IEEE exception flags in v1.

### Saturating combination

Conceptually:

```text
type SaturatingF32 = Float[Binary32] {
    rounding nearest_even
    subnormal preserve
    infinity forbid
    nan forbid
    overflow saturate
    divide_zero saturate
    invalid trap
}
```

Overflow clamps to the greatest finite value with the result's sign. A nonzero
value divided by zero also clamps to that signed finite bound. An operation with
no meaningful directed result, such as zero divided by zero or square root of a
negative value, traps rather than inventing a saturated value.

### Trapping combination

`TrappingF32`-style types forbid infinities and NaNs. Overflow, division by zero,
and invalid operations terminate with distinct deterministic traps. Trap
behavior does not vary by build mode.

### Checked combination

`CheckedF32`-style types forbid infinities and NaNs. Exceptional operations
return an ordinary error value:

```text
CheckedF32.checked_add(a, b) -> Result[CheckedF32, FloatError]
CheckedF32.checked_div(a, b) -> Result[CheckedF32, FloatError]
CheckedF32.checked_sqrt(x)   -> Result[CheckedF32, FloatError]
```

`FloatError` distinguishes at least:

```text
Overflow
DivideByZero
Invalid
```

Checked types do not initially implement `+`, `-`, `*`, or `/`. The homogeneous
operator protocol requires `T operation T -> T`, while checked operations return
`Result`. Named methods avoid associated output types and keep error behavior
visible.

## Standard Behavior Families

The standard library instantiates the four combinations for every v1 format:

```text
IEEE_F16       SaturatingF16       TrappingF16       CheckedF16
BF16           SaturatingBF16      TrappingBF16      CheckedBF16
IEEE_F32       SaturatingF32       TrappingF32       CheckedF32
IEEE_F64       SaturatingF64       TrappingF64       CheckedF64
```

IEEE, saturating, and trapping types implement homogeneous arithmetic operators.
Checked types expose the corresponding named checked methods.

The full policy name is intentional. It makes exceptional behavior visible and
searchable. Easy manual authoring remains a non-goal.

## Primitive Operations

The Core primitive catalog is parameterized by format and behavior rather than
duplicated as unrelated `f32_*` and `f64_*` concepts. It includes the minimum
operations required for:

- literals and exact source-to-format rounding;
- addition, subtraction, multiplication, division, and fused multiply-add;
- square root;
- ordered and unordered comparison;
- classification, sign inspection, and sign manipulation;
- explicit conversion to and from `Int` and other standard float formats;
- stable bit conversion using the standard encoding.

Formatting, parsing non-literal runtime strings, transcendental functions, and
domain-specific numerical algorithms belong to the standard library unless a
later measured requirement justifies a primitive.

## Native Lowering and Software Fallback

Target hardware availability never changes source semantics.

The backend may emit a native instruction only when the instruction sequence,
rounding, subnormal handling, exceptional behavior, and NaN normalization match
the declared type. Otherwise it inserts the required checks/normalization or
calls the conforming software implementation.

Examples:

```text
IEEE_F32 add       -> native binary32 add + canonical-NaN normalization as needed
SaturatingF32 add  -> native add + finite clamp/invalid handling
TrappingF32 div    -> native div with explicit exceptional checks
CheckedF32 div     -> checked sequence + Result construction
IEEE_F16 add       -> native FP16 when supported, software fallback otherwise
BF16 ordinary add  -> conforming native sequence or software fallback
```

Promotion to a wider native type followed by narrowing is permitted only when
proven bit-equivalent to direct evaluation in the declared format. The compiler
must not introduce double-rounding differences merely for speed.

The standard library supplies a deterministic reference implementation for all
four formats and behavior combinations. Constant evaluation uses the same
semantic reference. Native and reference results must be bit-identical after
canonical NaN normalization.

## User-Defined Bounded Float Types

Users may define a nominal type whose values are restricted to a finite interval
of a standard floating type. Each endpoint is independently inclusive or
exclusive:

```text
type Percent = IEEE_F16 {
    range {
        minimum 0.0 inclusive
        maximum 100.0 inclusive
    }
    range overflow error
}

type ProbabilityInterior = IEEE_F32 {
    range {
        minimum 0.0 exclusive
        maximum 1.0 exclusive
    }
    range overflow error
}
```

Despite the convenient description, `Percent` is not a subtype in the type
system. It is a distinct nominal validation type. It does not introduce general
subtyping, variance, or implicit coercion.

The bounds must be finite compile-time values, ordered from minimum to maximum,
and exactly representable in the base format. NaN and infinity cannot be bounds.
The compiler rejects an interval containing no representable base-format value,
including an open interval whose endpoints are adjacent representable values.

An exclusive zero endpoint excludes both positive and negative zero. For
example, `ProbabilityInterior` proves that every value is finite, positive, and
nonzero.

### Range policies

Bounded floats support exactly three domain-range policies:

```text
range overflow error
range overflow trap
range overflow saturate
```

Float range wrapping is excluded in v1 because modular floating-point domain
arithmetic has no broadly conventional meaning.

- `error` uses explicitly named checked operations returning `Result` and does
  not provide ordinary arithmetic operators.
- `trap` provides homogeneous operators and traps when the rounded base result
  is outside the range.
- `saturate` provides homogeneous operators and clamps a directed out-of-range
  finite or infinite result to the nearest representable value admitted by the
  interval. For an exclusive lower bound this is `next_up(minimum)`; for an
  exclusive upper bound it is `next_down(maximum)`. NaN or an invalid operation
  has no direction and traps.

The base format's behavior runs first:

```text
exact mathematical operation
    -> round to the base format
    -> apply base NaN/exception behavior
    -> apply the bounded type's range policy
```

Therefore a mathematical result slightly above `100` that rounds to exactly
`IEEE_F16(100)` is a valid inclusive `Percent` result. The same rounded value is
invalid if `100` is an exclusive upper endpoint. An IEEE NaN never belongs to a
bounded interval and is handled as an invalid range result.

If a bounded type excludes zero, the resolver and optimizer may treat its values
as proven nonzero. Division by such a denominator cannot produce the
divide-by-zero case, although the quotient may still overflow, underflow, or
round according to the result type.

### Construction and conversion

Ordinary construction is checked independently of the arithmetic policy:

```text
Percent(42.5)       // valid constant
Percent(101.0)      // compile-time error
Percent(input())    // Result[Percent, RangeError]
```

Conversion back to the base format is lossless but explicit:

```text
percent.to_base() -> IEEE_F16
```

There are no implicit conversions between bounded float types or from a bounded
type to its base. Mixed-type arithmetic requires explicit conversion or a named
library operation.

### Representation and lowering

A bounded float uses the same storage and ABI representation as its base format.
Its range and policy remain non-forgeable type metadata in typed Core. Native or
software base arithmetic is followed by the required range check, trap, or
clamp.

Changing a public bounded type's base format, bounds, base behavior, or range
policy is a source-compatibility and ABI change.

## Core Validation

The Core validator checks:

- every float type names a standard format and one allowed behavior combination;
- format and behavior metadata are complete and non-forgeable;
- checked types use result-producing operations rather than homogeneous
  operators;
- generated NaNs are canonical;
- bounded float values originate from valid construction or policy operations;
- range bounds are finite, ordered, exactly representable, and contain at least
  one representable value after endpoint openness is applied;
- native lowering preserves the declared semantics.

## Design Boundary

The system separates:

```text
Core mechanism:       Float[standard format, validated behavior]
Standard definitions: F16, BF16, F32, F64 behavior families
User domain types:    nominal finite ranges over standard definitions
Backend operations:   native instructions or deterministic software fallback
```

This mirrors the successful part of the integer design—explicit semantic types
and lower-IR specialization—without pretending that floating precision is only
an ordinary numeric range.

## References

- [IEEE 754-2019 floating-point standard](https://standards.ieee.org/ieee/754/6210/)
- [Intel AVX-512 FP16 and GCC support](https://www.intel.com/content/www/us/en/developer/articles/technical/building-innovation-and-performance-with-gcc12.html)
- [AMD AOCL hardware feature table](https://docs.amd.com/r/en-US/57404-AOCL-user-guide/8.1.5.-Hardware-Features)
- [Intel bfloat16 implementation notes](https://www.intel.com/content/www/us/en/developer/articles/technical/pytorch-on-xeon-processors-with-bfloat16.html)
- [Berkeley SoftFloat](https://qemu.googlesource.com/berkeley-softfloat-3/+/refs/heads/master/README.html)

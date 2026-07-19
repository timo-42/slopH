# Experimental Core v0

Core v0 is an executable experiment for testing a small typed intermediate
language. This document is normative for the authoritative C11 compiler's Core
v0 compatibility profile and the shared Core v0 corpus. It is not the final
SlopH Core, a source language, or the Bootstrap-Core profile.

The profile is deliberately small: pure, strict, monomorphic, and
deterministic. Its only primitive scalar is the exact integer. Named enums,
unary functions, and nine expression tags are sufficient to test resolved
names, typed binding, construction, case analysis, canonicalization, and
evaluation.

## Documents and lexical rules

A document is one tagged S-expression encoded as ASCII. Spaces, tabs, LF, and
CRLF are whitespace. A semicolon begins a comment through the next line break.
NUL, non-ASCII bytes, and bare CR are errors. Atoms cannot contain whitespace,
parentheses, or semicolons. Unknown tags, extra fields, and unsupported version
numbers are errors.

Global identities have the grammar:

```text
GLOBAL  = SEGMENT "::" SEGMENT ("::" SEGMENT)*
SEGMENT = [A-Za-z_][A-Za-z0-9_]*
LOCAL   = SEGMENT
```

Types and definitions are unordered top-level sets. Constructors and fields
are ordered declarations. A constructor identity must be the direct child of
its enum identity: constructor `pkg::Maybe::Some` belongs to
`pkg::Maybe`; `pkg::Maybe::nested::Some` does not.

## Grammar

The notation below uses `*` for repetition and `|` for alternatives. Those
characters are notation, not input syntax.

```text
unit       = (core 0 (types enum*) (defs definition*))

enum       = (enum GLOBAL constructor*)
constructor = (ctor GLOBAL field*)
field      = (field LOCAL data-type)
definition = (def GLOBAL type expression)

type       = Int
           | (named GLOBAL)
           | (fn type type)
data-type  = Int | (named GLOBAL)

binder     = (bind LOCAL type)

expression = (int INTEGER)
           | (local LOCAL)
           | (global GLOBAL)
           | (lam binder expression)
           | (app expression expression)
           | (let binder expression expression)
           | (prim PRIMITIVE expression*)
           | (con GLOBAL expression*)
           | (case expression type alternative*)

alternative = (alt GLOBAL binder* expression)
```

`INTEGER` is `0`, a non-zero decimal digit followed by decimal digits, or the
same preceded by `-`. Leading zeroes are rejected. `-0` is accepted but
canonicalizes to `0`. Integers are mathematical integers, bounded only by the
configured resource limits.

The complete primitive catalog is:

| Primitive | Type | Meaning |
| --- | --- | --- |
| `int.add` | `Int, Int -> Int` | exact addition |
| `int.sub` | `Int, Int -> Int` | exact subtraction |
| `int.mul` | `Int, Int -> Int` | exact multiplication |

Primitive calls are not curried. Their tag contains exactly the arguments in
the catalog entry.

## Static semantics

All types, binders, globals, constructors, and case result types are explicit.
Core v0 performs checking, not inference across missing annotations.

- Every referenced nominal type, constructor, global, and local must exist.
- Enum, constructor, definition, field, and binder identities are unique in
  their applicable scope. Binder spellings may not be reused anywhere within
  one definition, including disjoint case alternatives.
- Nominal types are closed enums. Recursive nominal data types and empty enums
  are valid. Function-typed constructor fields are forbidden in v0.
- A definition's expression must have its declared type.
- Lambda produces a unary function. Application requires the exact parameter
  type. `let` is non-recursive and its value is outside the new binder's scope.
- A constructor expression supplies every declared field once and in declared
  order, with its exact type.
- A case scrutinee has a named enum type. Alternatives contain every
  constructor of that enum exactly once and no foreign constructor. Each
  alternative binds the constructor fields in order and returns the case's
  explicit result type. An empty enum therefore has an empty exhaustive case.
- Global definition references form an acyclic graph. References under
  lambdas, cases, and otherwise unreachable expressions still participate, so
  a hidden or dead cycle is rejected.

There is no subtyping, polymorphism, recursion, effects, ownership, allocation
primitive, floating point, Boolean primitive, implicit conversion, partial
application of primitives, default case, wildcard, or nested pattern.

## Dynamic semantics

Evaluation is pure, strict, and left-to-right:

- application evaluates the function and then its argument;
- `let` evaluates the value and then the body;
- primitive arguments and constructor fields evaluate in source order;
- case evaluates its scrutinee and only the selected alternative body.

Lambdas are lexical closures. Globals are evaluated on demand and memoized for
one evaluation request; their first value or failure is reused. Exact integer
operations have no overflow behavior. The selected global may have any
declared type, but a final closure is rejected because Core v0 has no canonical
serialization for captured environments.

Successful evaluation prints a versioned value document:

```text
(value 0 (int 42))
(value 0 (con example::Maybe::Some (int 42)))
```

Constructor fields retain declared order. Only integers and constructor trees
are printable.

## Canonical form

`core print` parses and validates before printing. Canonical output:

- uses ASCII and LF with one final LF;
- removes comments and normalizes whitespace;
- prints integers in minimal decimal form;
- sorts enums and definitions lexicographically by global identity;
- preserves constructor and field declaration order;
- orders case alternatives by their enum's constructor declaration order;
- alpha-renames binders independently in each definition to `v0`, `v1`, ...
  in preorder, including case binders from left to right.

Canonical printing is deterministic and idempotent. It does not reorder
expressions, constructor fields, primitive arguments, or other constructs
whose order affects evaluation.

## Limits and diagnostics

Readers and evaluators enforce explicit positive limits for input bytes,
tokens, token size, syntax depth, AST nodes, literal digits, integer bits,
evaluation fuel, evaluation depth, allocated value nodes, and output bytes.
The implementation defaults are part of `sloph.core.Limits`; callers may
replace them. Exceeding a limit is an ordinary structured diagnostic, never an
ambient host-language exception.

| Limit | Default |
| --- | ---: |
| input bytes | 1,048,576 |
| tokens | 100,000 |
| bytes per token | 4,096 |
| syntax depth | 256 |
| AST nodes | 100,000 |
| digits per integer literal | 4,096 |
| evaluation fuel | 1,000,000 |
| integer magnitude in bits | 16,384 |
| evaluator and constructed-value depth | 4,096 |
| allocated value nodes | 100,000 |
| output bytes | 1,048,576 |

Fuel is charged deterministically. Entering any expression costs one unit; a
global reference costs one additional unit. After its argument expressions
have been evaluated, addition and subtraction cost
`1 + limbs(left) + limbs(right)`, while multiplication costs
`1 + limbs(left) * limbs(right)`, where
`limbs(x) = max(1, ceil(bit_length(abs(x)) / 64))`. Fuel is checked before a
charged operation. Literal and result magnitude is checked against
`integer_bits`; multiplication may reject before allocation when the operand
bit lengths prove the limit would be exceeded. Creating an integer, closure,
or constructor consumes one `value_nodes` entry. Pushing an evaluator
continuation consumes one `evaluation_depth` entry.

Diagnostics have a stable envelope for this experiment: schema
`sloph.diagnostic`, version `0`, code, phase, severity, message identity,
human message, byte-offset span, and structured details. Message prose and the
set of diagnostic codes remain unstable. JSONL diagnostics contain one object
per line on standard error.

## Command surface

The only command-line operations are:

```text
sloph unstable core check INPUT
sloph unstable core print INPUT [-o PATH]
sloph unstable core eval INPUT --symbol GLOBAL [--fuel NUMBER]
```

`INPUT` may be `-` for standard input. Success writes only the requested
canonical result to standard output. Diagnostics use standard error. Exit 0
means success, 1 means invalid Core or failed evaluation, 2 means invalid CLI
usage, 3 means an environment or I/O failure, and 4 is reserved for internal
compiler failures.

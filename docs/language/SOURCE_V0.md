# Experimental Source v0

Source v0 is the first authored SlopH syntax. It is a deliberately first-order,
fully annotated profile that lowers exactly to [Core v0](./CORE_V0.md). It
exists to test parsing, modules, inspection, elaboration, and native output; it
does not settle the final surface language.

## Project and modules

A project contains `sloph.json`:

```json
{
  "format": 1,
  "package": "demo",
  "source-root": "src",
  "entry": "demo::main::main"
}
```

These four fields are required and no others are accepted. `package` is one
lowercase ASCII identifier. `source-root` is a normalized relative directory.
`entry` is a fully qualified data value in the package.

Every `.sloph` file below the source root is a module. The mapping is fixed:
`src/foo/bar.sloph` in package `demo` declares `demo::foo::bar`. The declaration
must match the path.

Imports select public declarations explicitly:

```text
module demo::main;
import demo::math::{Number, add};
```

A module may contain at most one import declaration for each imported module;
all selections from that module are grouped in that declaration.

Declarations are private unless prefixed by `public`. Types, functions, and
values share one collision-checked module namespace. Constructors are selected
through their owning type. Imports have no aliases, wildcards, re-exports, or
transitive visibility. Module import cycles are invalid.

## Lexical and naming rules

Source is ASCII. Space, tab, LF, and CRLF are whitespace. NUL, non-ASCII bytes,
bare CR, and comments are errors. Identifiers contain ASCII letters, digits,
and underscores and do not begin with a digit.

- package, module, function, value, binder, and field names begin lowercase or
  underscore;
- type and constructor names begin uppercase;
- a constructor use is qualified through its type, such as `Maybe::Some`;
- imported selections are unqualified declaration names.

These casing rules make type, constructor, and ordinary call syntax
unambiguous without speculative parsing.

## Grammar and examples

Source v0 supports `Int`, nominal enum types, typed functions, typed values,
typed lets, construction, saturated calls, exhaustive cases, and the three
Core v0 integer primitives.

```text
public type Maybe {
  Some(item: Int);
  None();
}

public fn add(left: Int, right: Int) -> Int {
  primitive int.add(left, right)
}

value main: Int {
  let sum: Int = add(40, 2);
  case Maybe::Some(sum) -> Int {
    Maybe::Some(item: Int) => { item }
    Maybe::None() => { 0 }
  }
}
```

A block contains zero or more `let name: Type = expression;` bindings and one
final expression. A case states its result type and gives one type-qualified
constructor alternative per enum constructor. Alternative bodies are blocks.
Fieldless constructors retain explicit parentheses.

Integer literals use optional `-` and decimal digits. Input may contain leading
zeroes and negative zero; canonical formatting emits minimal decimal spelling.
Primitive spelling is exactly:

```text
primitive int.add(left, right)
primitive int.sub(left, right)
primitive int.mul(left, right)
```

Functions have at least one parameter. Calls directly name a visible function
and supply exactly its declared arguments. Source v0 rejects dynamic calls,
partial or excess application, function values, escaping functions, lambdas,
recursion, inference, generics, operators, macros, floats, effects, and I/O.
Top-level `value` is used for the zero-argument data entry point.

## Elaboration

Every declaration receives the resolved identity
`package::module-path::declaration`. Constructor identities append their
constructor name to their type identity. Source functions lower to Core v0
lambda chains and calls lower to application chains. Blocks lower to nested
non-recursive lets. Construction, primitives, and cases lower directly to the
corresponding Core forms.

All modules are discovered, header-scanned, topologically ordered, parsed,
resolved, lowered, and checked as one deterministic project unit. Core
validation remains the final independent check. The manifest entry must exist
and have `Int` or a named data type.

## Formatting and public Syntax

Source v0 has one formatter: two-space indentation, LF, minimal integers,
explicit punctuation, one final LF, and preserved import order. Declarations
are canonically grouped as types, functions, then values while preserving
order inside each group. Formatting is deterministic and idempotent.

The public AST JSON envelope is:

```json
{"schema":"sloph.syntax","version":0,"module":{}}
```

Nodes are objects with an explicit `kind`, byte-offset `span`, and the complete
fields for that kind. Arrays preserve source order. Unknown or missing fields,
node kinds, schema versions, invalid categories, and invalid names are errors.
JSON uses sorted keys, ASCII escaping, compact separators, and one final LF.

Source parsing, formatting, AST encoding, project reads, and elaboration use
explicit resource limits and structured diagnostics. The default project caps
are 10,000 source files and 268,435,456 total source bytes; every individual
source remains subject to the smaller input limit.

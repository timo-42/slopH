# Research: Extensible Programming Languages

Research summary as of July 2026. Sources are primary papers, official
documentation, and author or institutional copies. No unauthorized repositories
were used.

## Conclusion

The most successful extensible languages do not make every part of the language
extensible. They preserve a small, fixed semantic center and put carefully
delimited extension points around syntax and compile-time computation.

The recurring pattern is:

```text
fixed lexer/parser envelope
    -> hygienic, explicitly imported surface macros
    -> fixed type checker and semantics
    -> canonical typed Core
```

Systems become harder to understand, compile, secure, and tool when extensions
can independently change tokenization, parsing, name resolution, typing,
optimization, and code generation. Extensibility is therefore a budget, not an
unqualified virtue.

## Approaches and Evidence

### Scheme and Racket: hygienic language extension

Scheme established the central idea that macros should manipulate syntax while
preserving lexical scope. Hygienic expansion prevents identifiers introduced by
a macro from accidentally capturing, or being captured by, identifiers at the
call site. This is substantially safer than textual substitution.

Racket develops the idea into “languages as libraries.” Syntax objects carry
source locations and binding information; modules provide phase separation;
macros can implement substantial language layers; and `#lang` selects a
language. Racket demonstrates that language-oriented programming can work in a
real ecosystem and that extensions can coexist with normal module composition.

What works:

- hygiene makes local binding behavior compositional;
- syntax objects preserve source and scope information through expansion;
- explicit phases distinguish run-time from expansion-time bindings;
- macros can build DSLs without modifying the compiler;
- expansion can be inspected and tested.

What it costs:

- “hygiene” is not one small trick: scopes, certificates or scope sets, phases,
  syntax parameters, source provenance, and deliberate capture all add rules;
- macro errors often expose the expanded language rather than the authored one;
- phase-level mistakes are difficult even for experienced users;
- a framework capable of defining whole languages is itself too large to fit
  this project's whole-language context-size goal.

The design lesson is to adopt lexical hygiene and source-carrying syntax values,
but not Racket's full tower of language construction facilities in version 1.

Sources:

- William Clinger and Jonathan Rees,
  [Macros That Work](https://scholarsbank.uoregon.edu/bitstreams/12fed7c3-fd49-4ae8-97e4-6bbb156aaba6/download)
  (POPL 1991)
- Eugene Kohlbecker et al.,
  [Hygienic Macro Expansion](https://web.cs.ucdavis.edu/~devanbu/teaching/260/kohlbecker.pdf)
  (LFP 1986)
- Matthew Flatt,
  [Binding as Sets of Scopes](https://www-old.cs.utah.edu/plt/publications/popl16-f.pdf)
  (POPL 2016)
- Matthias Felleisen et al.,
  [Languages as Libraries](https://www-old.cs.utah.edu/plt/publications/pldi11-tscff.pdf)
  (PLDI 2011)

### Template Haskell: compiler-integrated metaprogramming

Template Haskell provides quotation, splicing, reification, and declaration
generation. It is expressive enough to remove boilerplate and derive programs
from type information. Staging checks prevent some nonsensical cross-stage
references.

Its difficulties show why a macro system should not expose the entire compiler:

- expansion depends on declaration order and staging rules;
- reification couples macros to compiler AST and type representations;
- macro execution can perform I/O, fail, diverge, or depend on the host;
- generated declarations can make errors and builds hard to reproduce;
- compiler-version changes can break metaprograms even when user syntax is
  unchanged.

Template Haskell is strong evidence for quotation and explicit stages, but weak
evidence for unrestricted compile-time authority.

Source: Tim Sheard and Simon Peyton Jones,
[Template Meta-programming for Haskell](https://www.cs.tufts.edu/comp/150PLD/Papers/TemplateHaskell.pdf)
(Haskell Workshop 2002).

### Rust procedural macros: explicit but operationally expensive

Rust procedural macros consume and produce token streams. Derives, attributes,
and function-like invocations are syntactically obvious, and the separation into
dedicated crates gives the build system a visible dependency boundary.

The tradeoffs are important:

- procedural macros are largely unhygienic and must manage paths and generated
  names carefully;
- they execute compiler-hosted code with essentially the same filesystem and
  environment access as build scripts;
- arbitrary code can hang, panic, read ambient state, or make builds
  non-reproducible;
- token streams are small as an interface, but useful macros often need large
  parsing and quoting libraries;
- generated code and dependency compilation can materially increase build time.

The useful idea is the explicit token/syntax transformation boundary. The part
to reject is unrestricted ambient authority.

Sources:

- [Rust Reference: procedural macros](https://doc.rust-lang.org/reference/procedural-macros.html)
- [Rust Reference: macro hygiene](https://doc.rust-lang.org/reference/macros-by-example.html#hygiene)

### Nim and Julia: macros over compiler-shaped ASTs

Nim exposes typed and untyped AST macros; Julia exposes quoting, macro expansion,
hygiene rules, and generated functions. Both make metaprogramming convenient in
their native ecosystems, but both reveal a maintenance hazard: user code comes
to depend on the compiler's current AST, expansion timing, inference behavior,
and internal distinctions.

Julia's separation between macros and generated functions is instructive. A
macro transforms syntax before type information is available; a generated
function is selected using argument types under additional restrictions. Mixing
those roles into one mechanism makes dependency analysis much harder.

Sources:

- [Nim manual: macros](https://nim-lang.org/docs/manual.html#macros)
- [Julia manual: metaprogramming](https://docs.julialang.org/en/v1/manual/metaprogramming/)

### Typed staging, partial evaluation, and `comptime`

MetaML and later typed multi-stage systems make code generation explicit in the
type system. Lightweight Modular Staging embeds staged code generation in a
host language and uses types to prevent many malformed generated programs.
These systems support optimization and domain-specific generators with much
stronger guarantees than string-based generation.

The price is a larger type theory: quotations, code types, stage indices,
cross-stage persistence, and effects must all be explained. This is a poor fit
for a language whose complete rules must remain short.

Zig's `comptime` shows a simpler user model: ordinary-looking code is evaluated
at compile time when values are known. This avoids a second textual macro
language. It works best because the surface grammar and compiler semantics stay
fixed. It does not by itself permit user-defined syntax.

Sources:

- Walid Taha and Tim Sheard,
  [Multi-Stage Programming with Explicit Annotations](https://web.cecs.pdx.edu/~sheard/papers/msp.pdf)
- Tiark Rompf and Martin Odersky,
  [Lightweight Modular Staging](https://infoscience.epfl.ch/record/150347/files/gpce63-rompf.pdf)
- [Zig language reference: `comptime`](https://ziglang.org/documentation/master/#comptime)

For this project, one small, pure evaluator shared by constant evaluation and
macro execution is preferable to a separate staging type system.

### SugarJ and ableC: modular syntax and compiler extensions

SugarJ treats syntax extensions as importable modules. This is attractive
because syntax use becomes an explicit dependency, rather than mutable global
parser state. Its work also exposes the hard part: independently useful grammar
extensions can create ambiguities when composed, parser generation becomes part
of compilation, and editors must know which grammar applies to each file.

ableC extends C using attribute grammars. Extensions can contribute syntax,
analysis, and translation, and the system statically checks useful composition
properties. This is serious evidence that modular compiler extensions are
possible. It is also evidence that doing so requires a large meta-language,
attribute-grammar infrastructure, restrictions on extensions, and specialized
tooling.

Sources:

- Sebastian Erdweg et al.,
  [SugarJ: Library-based Syntactic Language Extensibility](https://www.cs.cmu.edu/~ckaestne/pdf/oopsla_sugarj.pdf)
  (OOPSLA 2011)
- Ted Kaminski et al.,
  [Reliable and Automatic Composition of Language Extensions to C](https://www-users.cse.umn.edu/~evw/pubs/kaminski17oopsla/kaminski17oopsla.pdf)
  (OOPSLA 2017)

Explicit, non-transitive syntax imports are worth adopting. User-defined grammar
productions are not.

### Mixfix operators and parser extensibility

Mixfix notation can express familiar mathematical forms, but composable parsing
needs precedence relations, associativity rules, name resolution, and useful
ambiguity reporting. Formal approaches can solve much of this, yet their
algorithms and specification burden are not small; ambiguous inputs may require
expensive exploration.

For a compact AI-oriented language, fixed application and delimiter syntax plus
a restricted infix table is a better trade. User extensions may choose a symbol
from a lexer-defined operator class and declare precedence from a small fixed
range. They should not introduce new token classes, delimiters, or arbitrary
grammar shapes.

Source: Nils Anders Danielsson and Ulf Norell,
[Parsing Mixfix Operators](https://www.cse.chalmers.se/~nad/publications/danielsson-norell-mixfix.pdf)
(IFL 2009).

### Language workbenches: Spoofax, Rascal, and MPS

Language workbenches coordinate grammars, name analysis, transformations,
editors, and sometimes projectional editing. Spoofax demonstrates that a DSL
can bring its own IDE services; MPS can avoid textual parsing ambiguity by
editing syntax trees directly.

These systems succeed for organizations willing to adopt an ecosystem. They do
not make a small self-contained language: the workbench, generator languages,
editor protocol, project model, and generated artifacts become part of the
effective language. Projectional editors also reduce compatibility with plain
text tools and AI training corpora.

Source: Lennart Kats and Eelco Visser,
[The Spoofax Language Workbench](https://eelcovisser.org/publications/2010/KatsV10.pdf)
(OOPSLA 2010).

### Extensible type systems and fexprs

Turnstile demonstrates that macro expansion can implement type systems and
typed languages. It is elegant research, but it deliberately transfers trusted
compiler responsibilities to extension authors. If users can redefine typing,
then “well typed” no longer has one compact meaning, and Core validation must
either understand every extension or trust generated evidence.

Fexprs go further: operative functions receive their operands without automatic
evaluation and can decide how to interpret them. This makes control and binding
extensible from within the language, but frustrates ordinary local reasoning,
static analysis, and compilation because a call's evaluation behavior is no
longer known from its syntax.

Sources:

- Stephen Chang et al.,
  [Type Systems as Macros](https://www.cs.tufts.edu/~nr/cs257/archive/stephen-chang/type-systems-as-macros.pdf)
  (POPL 2017)
- Andras Kovacs,
  [Fexprs as the Basis of Lisp Function Application](https://arxiv.org/abs/2303.12254)
  (2023)

Neither user-defined typing rules nor fexpr-style evaluation should be part of
the initial language.

## Why Extensible Systems Commonly Fail to Scale

“Fail” here means failing to achieve broad, predictable, tool-friendly use—not
that the systems produced no value.

1. **Composition is harder than individual extension.** Two grammars, binding
   rules, or transformations may each work alone and conflict together.
2. **The effective language becomes dependency-specific.** Reading a file now
   requires executing or understanding its extension graph.
3. **Compile-time code gains ambient authority.** I/O, environment access,
   nondeterminism, and divergence weaken security and reproducibility.
4. **Tools see a moving language.** Parsers, formatters, indexers, debuggers, and
   AI models must either expand code or reimplement extension semantics.
5. **Diagnostics cross abstraction boundaries.** Errors originate in generated
   code unless syntax objects retain provenance and the compiler remaps them.
6. **Compiler internals become public APIs.** AST and type representation changes
   become ecosystem-breaking changes.
7. **Optimization becomes unpredictable.** Extensions can duplicate code,
   obscure costs, or rely on optimizer phase ordering.
8. **Documentation size moves rather than disappears.** A tiny kernel plus an
   unconstrained meta-language is not a tiny complete language.

## Recommended Extension Model for This Language

### Include

- A fixed tokenizer and grammar envelope.
- Hygienic macros operating on structured syntax, never raw text.
- An invocation form recognizable without executing user code: a leading macro
  name and delimited or grammar-bounded payload.
- Explicit, non-transitive imports for syntax providers.
- Exactly two phases: compile time and run time.
- A pure, deterministic, resource-bounded compile-time evaluator.
- Quotation and splicing over a stable public syntax representation.
- Source spans and expansion provenance on every generated node.
- A command that displays canonical expansion.
- A restricted, lexer-defined infix operator class with explicit precedence and
  associativity.
- Expansion to ordinary surface forms followed by normal type checking and
  lowering to the one fixed Core.

### Exclude initially

- Reader macros and mutable tokenization.
- Arbitrary grammar productions or new delimiters.
- User-defined name resolution, type rules, optimizer passes, or lowering.
- Compile-time filesystem, network, clock, random, process, or environment
  access.
- Macros that depend on compiler-private ASTs.
- Same-module syntax generation that changes how earlier or later text parses.
- Recursive expansion without a specified depth, step, and output-size limit.

### A compact semantic contract

A macro can be described as:

```text
expand : SyntaxInput -> Result<SyntaxOutput, Diagnostic>
```

where expansion is hygienic, deterministic, explicitly imported, bounded, and
source-tracked. Its output must parse as a fixed set of ordinary declaration or
expression forms and pass the same resolver, type checker, and Core validator as
hand-written code.

That contract supplies most of the expressive benefit users seek from Lisp,
Racket, Rust, and `comptime`, without making the compiler itself a user-extensible
object.

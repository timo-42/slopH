# Research: Haskell and GHC Core

Research summary as of July 2026. Sources are official documentation, primary
papers, and author or institutional copies. No unauthorized repositories were
used.

## Conclusion

GHC strongly validates this architecture:

```text
rich surface language -> small typed Core -> validated transformations
                      -> operational lower IR -> native code
```

It does **not** validate the claim that a small list of Core expression
constructors makes the complete language or compiler small. GHC's complexity
lives primarily in its type and coercion calculus, lazy operational semantics,
primitive catalog, optimizer heuristics, interface summaries, lower IRs, and
runtime system.

The appropriate lesson is to adopt typed Core and Core validation while
avoiding the features that forced GHC Core and its runtime to grow.

## What Works Well

### A stable semantic center

GHC Core has remained comparatively stable while surface Haskell gained many
features. Marlow and Peyton Jones describe Core as the shared language for a
large sequence of independently developed transformations. A surface feature
that elaborates cleanly into Core can remain sugar; a feature requiring a Core
change deserves much greater scrutiny.

This separation also lets GHC retain source-specific information during
renaming and type checking, then discard surface complexity before
optimization. The cost is a large front end, but errors can be expressed in
terms of authored Haskell instead of generated Core.

Source: Simon Marlow and Simon Peyton Jones,
[The Glasgow Haskell Compiler](https://aosabook.org/en/v2/ghc.html) (2012),
with [author PDF](https://simonmar.github.io/bib/papers/aos.pdf).

### Explicitly typed transformations and Core Lint

Core Lint independently checks Core after transformations. It catches invalid
scope, types, applications, coercions, representations, and other invariants
near the pass that introduced the problem. It also audits the surface type
checker because desugared output must pass Core validation.

Lint cannot detect a type-correct misoptimization, but compiler authors report
that type correctness catches many transformation bugs in practice.

Sources:

- [Current GHC Core Lint implementation](https://downloads.haskell.org/ghc/9.14.1/docs/libraries/ghc-9.14.1-da80/src/GHC.Core.Lint.html)
- [External GHC Core representation](https://downloads.haskell.org/~ghc/7.6.3/docs/html/users_guide/an-external-representation-for-the-ghc-core-language-for-ghc-6.10.html)

### Typeclasses and dictionary elaboration

Typeclasses are one of Haskell's strongest successes. They provide principled
ad-hoc polymorphism and elaborate conceptually into dictionary values and
ordinary function arguments. When dictionaries are visible, inlining or
specialization can remove dispatch and expose further optimization.

Source: Philip Wadler and Stephen Blott,
[How to Make Ad-hoc Polymorphism Less Ad Hoc](https://users.csc.calpoly.edu/~akeen/courses/csc530/references/wadler.pdf)
(POPL 1989).

### A layered compiler

Core is a typed transformation language; STG makes closures and lazy evaluation
operational; Cmm exposes low-level control and representation; backends produce
native, LLVM, JavaScript, or WebAssembly output. This is a strong separation of
concerns. It also demonstrates that one IR rarely serves source diagnostics,
high-level optimization, closure conversion, and machine lowering equally
well.

Sources:

- Simon Peyton Jones,
  [Implementing Lazy Functional Languages on Stock Hardware](https://www.cs.tufts.edu/comp/150FP/archive/simon-peyton-jones/spineless-jfp.pdf)
- [Current GHC backend documentation](https://ghc.gitlab.haskell.org/ghc/doc/users_guide/codegens.html)

### Productive functional abstraction

Laziness and higher-order functions allow producers and consumers to be
composed without fixing evaluation order or intermediate data structures.
Stream fusion, rewrite rules, inlining, and specialization can then remove
abstraction costs. This is a genuine success rather than merely elegant syntax.

Sources:

- John Hughes,
  [Why Functional Programming Matters](https://www.cse.chalmers.se/~rjmh/Papers/whyfp.pdf)
- Simon Peyton Jones et al.,
  [Playing by the Rules](https://www.microsoft.com/en-us/research/publication/playing-by-the-rules-rewriting-as-a-practical-optimisation-technique-in-ghc/)
- Simon Peyton Jones,
  [Call-pattern Specialisation for Haskell Programs](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/spec-constr.pdf)

## Shortcomings and Hidden Costs

### “Tiny Core” hides a large type language

System FC was introduced because ordinary System F could not directly express
GADTs, associated types, open type functions, and functional dependencies.
Explicit equality witnesses and casts provide one general mechanism and erase
at runtime, which is elegant. But the proof language, consistency conditions,
roles, kind equality, and inference rules significantly enlarge Core's actual
semantics.

Later work was required for explicit kind equality and safe zero-cost coercion.
Advanced inference also loses familiar Hindley-Milner properties such as
straightforward principal types and local generalization.

Sources:

- Sulzmann et al.,
  [System F with Type Equality Coercions](https://cgi.cse.unsw.edu.au/~reports/papers/0614.pdf)
- Weirich, Hsu, and Eisenberg,
  [System FC with Explicit Kind Equality](https://www.seas.upenn.edu/~sweirich/papers/fckinds-extended.pdf)
- Breitner et al.,
  [Safe Zero-cost Coercions for Haskell](https://www.seas.upenn.edu/~sweirich/papers/coercible.pdf)
- Vytiniotis et al.,
  [OutsideIn(X)](https://simon.peytonjones.org/outsideinx/)

For this project, the implication is clear: omit GADTs, open type families,
representational equality, dependent kinds, and user-visible coercion proofs in
v1. Do not add `Cast` or `Coercion` merely because GHC has them.

### Laziness makes cost non-local

Laziness enables composition but makes time and especially space retention
difficult to predict. An unevaluated thunk may retain a large object graph;
chains of deferred work may accumulate; let-floating and full-laziness
transformations can change retention behavior.

GHC therefore needs strictness analysis, thunk update machinery, black holes,
partial applications, cost-centre stacks, heap and retainer profiling, pointer
tagging, and substantial runtime support. Research on optimized Haskell
causality exists because source-to-runtime performance attribution is hard.

Sources:

- [GHC profiling guide](https://ghc.gitlab.haskell.org/ghc/doc/users_guide/profiling.html)
- Wortmann and Duke,
  [Causality of Optimized Haskell](https://eprints.whiterose.ac.uk/id/eprint/77401/)
- Marlow et al.,
  [Faster Laziness Using Dynamic Pointer Tagging](https://simonmar.github.io/bib/papers/ptr-tagging.pdf)

Recommendation: strict evaluation by default. If laziness is needed, expose an
explicit `Lazy[T]` abstraction with visible `delay` and `force` operations.

### Optimization is heuristic and phase-sensitive

GHC's higher-order, dictionary-heavy style relies heavily on inlining and
simplification. Its implementors describe production inlining as a “black art”
of profitability, duplication, recursion, code size, and future opportunities.

Rewrite rules and pragmas have activation phases because inlining can expose or
destroy later matches. Rules may fail to fire or cause divergent rewriting.
GHC has a simplifier tick budget and stops excessive optimization. This is
direct evidence that user-extensible rewriting requires fuel and termination
controls.

Sources:

- Peyton Jones and Marlow,
  [Secrets of the Glasgow Haskell Compiler Inliner](https://www.cs.tufts.edu/comp/150FP/archive/simon-peyton-jones/secrets-of-the-glasgow-haskell-compiler-inliner.pdf)
- [Current optimization options](https://downloads.haskell.org/ghc/latest/docs/users_guide/using-optimisation.html)
- [Current RULES documentation](https://ghc.gitlab.haskell.org/ghc/doc/users_guide/exts/rewrite_rules.html)

Recommendation: use a short, fixed optimization pipeline with explicit size
and fuel budgets. Do not expose user-controlled optimization phases in v1, and
never make correctness depend on an optimization firing.

### Typeclass coherence and performance

Dictionary dispatch may remain in hot loops when specialization or inlining
fails. Cross-module optimization is heuristic and increases compile time,
interface size, and binary size. Open-world instances conflict with modular
coherence; orphan and overlapping instances can make downstream combinations
fail even when each library compiled independently.

Sources:

- [GHC performance hints](https://ghc.gitlab.haskell.org/ghc/doc/users_guide/hints.html)
- Bottu et al.,
  [Coherence of Type Class Resolution](https://arxiv.org/abs/1907.00844)
- [GHC separate compilation](https://ghc.gitlab.haskell.org/ghc/doc/users_guide/separate_compilation.html)

Recommendation: start with single-parameter homogeneous classes, no overlap,
no orphans, no associated types, and no default-method hierarchy. Require an
instance to live with either its class or its type.

### Separate compilation versus optimization

GHC interface files carry exported types, instances, rules, unfoldings, demand
information, and fingerprints. This enables module-at-a-time compilation and
cross-module optimization, but cross-module inlining weakens ABI stability:
clients embed definitions and must be recompiled when implementations change.

GHC documentation recommends reducing optimization when compilation latency
matters. There is no reliable universal number for how much slower Haskell
compilation is; the defensible conclusion is simply that richer optimization
and interface summaries have measurable compile-time and invalidation costs.

Recommendation: separate a compact, versioned semantic interface from optional
optimization summaries. Fingerprint exported semantics independently of
private bodies, and keep cross-module optimization bounded and optional.

### Diagnostics become detached from source

Desugaring, macro expansion, inlining, fusion, and specialization destroy a
simple source-to-runtime relationship. GHC mitigates this by typechecking before
desugaring and carrying source/profiling metadata, yet advanced performance
debugging commonly requires Core dumps and heap profiles.

Recommendation: every transformed node needs a durable origin chain:

```text
authored source -> macro invocation/definition -> expanded surface -> typed Core
```

Diagnostics should default to authored syntax and offer expansion/Core details
on demand.

### Primitives and runtime are part of the language

GHC's primitive operations and runtime representation are an unsafe semantic
backdoor beneath the small Core grammar. Some primitives rely on caller-side
conditions. The closure, thunk, scheduler, garbage collector, and concurrency
runtime is far larger than the ten `Expr` constructors suggest.

Recommendation: specify every primitive's type, effects, traps, target support,
and ownership behavior. A tiny Core with an open or undocumented primitive set
is not a tiny language.

## Design Decisions Supported by the Evidence

Adopt:

- a small explicitly typed semantic Core;
- an independent Core validator after transformations;
- a separate lower representation for closure, ownership, and machine details;
- canonical expanded-source and Core output;
- source-origin chains through macros and optimization;
- compact versioned module interfaces;
- strict evaluation by default;
- a small fixed optimizer with deterministic budgets.

Avoid initially:

- pervasive call-by-need and an STG-style thunk runtime;
- System FC coercions, GADTs, and open type families;
- overlapping or orphan typeclass instances;
- uncontrolled rewrite rules and user-selected optimizer phases;
- runtime performance that depends on fragile cross-module inlining;
- an unspecified or unbounded primitive catalog.

Measure language size as:

> Surface elaboration rules + Core term grammar + Core type/effect/ownership
> system + operational semantics + primitive catalog + module/interface rules.

## Evidence Cautions

- Many architectural papers were written by GHC implementors. They are strong
  primary evidence about design and measurements, but qualitative success
  claims are partly self-assessment.
- Historical benchmark results demonstrate feasibility, not expected results
  on current hardware or workloads.
- Current official documentation reliably describes behavior but is not a
  controlled empirical comparison.
- This report excludes anecdotal compilation-speed ratios that lack a stable,
  apples-to-apples methodology.

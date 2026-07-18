# Memory Management Research

Research summary as of July 2026. This document evaluates memory-management
models against the project's top-level goals: a small language, predictable
AI-generated code, human readability, fast compilation, general-purpose use,
and native executables.

## Main Conclusion

There are simpler ownership models than Rust's. The best current candidate for
this language is:

> Unique ownership with non-storable, call-scoped borrows and no lifetime
> parameters.

This retains deterministic destruction and compile-time memory safety while
deliberately giving up references that can be stored, returned, captured, or
shared arbitrarily.

Garbage collection is not inherently a large compilation-time overhead. A
simple compiler can emit allocation calls and use a tracing collector. The
difficult part is generally the runtime: pause times, memory overhead,
concurrency, root tracking, and foreign-function interaction. A conservative
collector can require very little compiler support, as demonstrated by the
[Boehm-Demers-Weiser collector][boehm].

## Comparison

| Model | Good | Bad |
| --- | --- | --- |
| Tracing garbage collection | Very small source-language model; cycles work naturally; compiler can remain simple | Runtime pauses and additional memory; deterministic resource cleanup needs a separate mechanism |
| Automatic reference counting | Deterministic reclamation; simple source syntax; costs are distributed | Cycles leak unless weak references or cycle collection are added; retain/release traffic costs time |
| Rust-style ownership | No tracing runtime; deterministic destruction; supports safe, highly expressive borrowing | Complex lifetime, alias, reborrow, closure, generic, and destructor interactions |
| Strict linear types | Small and mathematically clean checker; prevents leaks, double frees, and use-after-free | Programs must explicitly consume or return resources, producing verbose code |
| Regions and arenas | Extremely cheap allocation and bulk destruction; cycles inside a region are harmless | Objects with unrelated lifetimes are difficult; inference or annotations add complexity |
| Mutable value semantics | No lifetime parameters; local reasoning; safe in-place mutation | Direct cyclic graphs, stored references, parent pointers, and mutable sharing are restricted |
| Inferred borrowing with RC fallback | Very simple surface language with potentially low RC overhead | Sophisticated and recent compiler analysis; current evidence is mainly for pure functional programs |

## Rust-Style Ownership

Rust combines unique ownership, moves, shared and exclusive borrowing,
non-lexical lifetimes, lifetime parameters, destructors, closures, traits,
concurrency, and unsafe library abstractions.

### Good

- Memory and data-race safety without tracing garbage collection.
- Deterministic destruction and low-level control.
- Borrowed access can compile to ordinary pointers with no reference-counting
  operation.
- Supports efficient slices, iterators, views, and other non-owning values.

### Bad

- Stored and returned references require lifetime relationships in the type
  system.
- Reborrowing, aliasing, mutation, variance, destructors, closures, and async
  execution interact in nontrivial ways.
- The compiler needs flow-sensitive reasoning about loans and liveness.
- The complete mental model and documentation are too large for this project's
  intended core language.

[Oxide][oxide] formalizes a source-level core of Rust and illustrates how much
machinery remains even after unrelated Rust features are removed.
[RustBelt][rustbelt] formally studies the contracts between safe Rust and
unsafe library implementations. Rust's work on non-lexical lifetimes and the
continuing [Polonius borrow-checker project][nll] demonstrate the implementation
cost of highly expressive borrowing.

## Tracing Garbage Collection

### Good

- The smallest and most permissive language semantics.
- Arbitrary graphs and cycles work without annotations.
- Type checking does not need ownership or lifetime analysis.
- A basic compiler can compile quickly by delegating collection to a runtime.

### Bad

- Stop-the-world collectors can introduce unpredictable pauses.
- Good throughput commonly requires spare heap capacity.
- Precise collectors require stack maps, safe points, and cooperation with
  generated native code.
- Files, locks, sockets, and similar resources still require deterministic
  cleanup separate from memory reclamation.
- Production-quality collectors are substantial runtime projects.

The [Immix paper][immix] shows that tracing collectors can achieve strong space
and performance results, but also demonstrates that collector quality is a
serious implementation topic. Research measuring production collectors found
meaningful CPU and wall-clock costs under constrained heap sizes
([Distilling the Real Cost of Production Garbage Collectors][gc-cost]).

Conclusion: tracing GC is a valid choice if language simplicity is the only
priority. Its primary cost is runtime behavior, not compiler latency.

## Automatic Reference Counting

### Good

- Objects are normally reclaimed as soon as their last owner disappears.
- Destruction is deterministic, which integrates naturally with resource
  management.
- A basic compiler only needs to insert retain and release operations.
- There is no global tracing pause for acyclic data.

### Bad

- Strong reference cycles leak memory.
- Weak and unowned references add language rules and new failure modes.
- Closures can create non-obvious cycles by capturing their owner.
- Every reference-count update costs time; thread-safe counts can require
  atomic operations.
- Eliminating unnecessary retain/release operations requires compiler analysis.

[Swift ARC][swift-arc] documents both object cycles and closure capture cycles,
showing that ARC moves some memory reasoning into strong, weak, and unowned
reference design.

[Perceus][perceus] inserts precise reference-count operations and reuses
allocations for cycle-free functional programs. It reports competitive results,
but its guarantee explicitly applies to cycle-free programs. Lean 4 also uses
reference counting, borrowed calling conventions, uniqueness tests, and
in-place reuse in its [runtime][lean-runtime].

Conclusion: RC is a useful implementation mechanism or later shared-ownership
feature, but it should not silently become the core semantic model unless cycle
behavior is resolved.

## Strict Linear Types

Linear values must be consumed exactly once. Affine variants allow a value to
be consumed at most once, including implicit destruction.

### Good

- The ownership checker is smaller than a general Rust borrow checker.
- Use-after-free and double-free errors are statically prevented.
- Linear values also model files, sockets, locks, and capabilities.
- No tracing collector or reference counter is required.

### Bad

- Functions often need to return a resource merely so the caller can continue
  using it.
- Error handling and branching must account for every linear value.
- The resulting value threading increases program and documentation size.
- Borrowing conveniences can gradually recreate lifetime-system complexity.

[Austral][austral] is the most relevant practical design. Its explicit goal is
"fits-in-head simplicity," and its [linear-type tutorial][austral-linear]
explains the safety and resource-management benefits.

Conclusion: this is a strong academic baseline, but strict linearity is likely
too verbose for readable generated application code. Affine unique ownership is
a better default.

## Mutable Value Semantics and Second-Class Borrows

Mutable value semantics bans sharing of mutable state rather than banning
mutation. In its simplest form, references exist only temporarily at function
boundaries. They cannot be placed in variables or fields.

### Good

- No lifetime parameters or lifetime subtyping.
- Mutation remains efficient and locally understandable.
- Borrow checking is local because borrows cannot escape calls.
- Variables cannot silently share mutable state.
- The model is compact enough to explain using ownership plus a few parameter
  modes.

### Bad

- Functions cannot return borrowed slices or views.
- Records cannot contain direct references to other records.
- Self-referential objects and conventional cyclic structures are impossible.
- Parent pointers and mutable shared caches require handles, arenas, copying,
  or another explicit abstraction.
- Some programs may copy more data unless the implementation performs reuse or
  copy-elision optimizations.

The [Native Implementation of Mutable Value Semantics][mvs] paper describes
second-class references as a simpler alternative to sophisticated
flow-sensitive type systems. [Hylo][hylo] is the main language project
developing this model for native systems programming.

Conclusion: this is the best-researched match for the project's small-language
and fast-compilation requirements.

## Regions and Arenas

Regions allocate multiple objects into one area and reclaim all of them
together.

### Good

- Allocation can be a pointer bump.
- Deallocation is a single bulk operation.
- Cycles within a region do not require tracing or weak references.
- Excellent for request-scoped data, parsers, compilers, games, and temporary
  calculations.

### Bad

- A short-lived object can keep an entire region alive.
- Objects with different or changing lifetimes are awkward.
- Automatic region inference can become sophisticated.
- Explicit region parameters spread through APIs.

[Cyclone][cyclone] combined regions with safe C-style programming but reported
that regionizing programs could require programmer work. [Project Verona][verona]
explores isolated region forests and reference capabilities; it offers
predictable localized costs but is not a small replacement for simple
ownership.

Conclusion: arenas should be a standard library abstraction rather than the
universal memory model.

## Automatic Borrow Inference

Recent work combines borrowing with reference counting as a fallback. The
compiler infers when a value can be borrowed; when it cannot prove that, it
inserts reference-count operations.

### Good

- Lifetime annotations disappear from source code.
- Programs that are easy to analyze pay few reference-counting costs.
- Safe programs do not need to be rejected merely because borrowing inference
  fails.

### Bad

- Complexity is moved into global compiler analysis.
- Compile-time predictability needs further evaluation.
- The strongest current result targets a pure functional language, not broad
  imperative application software.
- The work is too recent to treat as an established general-purpose design.

The 2026 paper [Fully-Automatic Type Inference for Borrows with Lifetimes][auto-borrow]
reports a 75--100% reduction in reference-count increments on affected
benchmarks and a 1.48x geometric-mean speedup over its comparison baseline.

Conclusion: promising future optimization research, but not a suitable
foundation for the first compiler.

## Recommended Initial Model

The initial language should explore the following deliberately restricted
model:

1. Every heap value has exactly one owner.
2. Assignment moves nontrivial values; small scalar values copy.
3. Cloning an owned heap value is explicit.
4. Function parameters can consume, temporarily read, or temporarily mutate a
   value.
5. A temporary borrow lasts only for the function call.
6. A borrow cannot be stored, returned, captured, or sent to another task.
7. Function results are owned values.
8. Closures capture owned values by move.
9. Values are destroyed deterministically at scope exit, with an explicit
   early-drop operation when needed.
10. Graphs use an owning arena plus typed or generational handles instead of
    direct references.
11. Shared ownership, weak references, and general lifetime parameters are not
    included initially.

The compiler then tracks only a small set of states for each non-copy value:
available, moved, temporarily read, or temporarily mutated. Since borrows do
not escape calls, function signatures are sufficient for local checking and no
general lifetime solver is required.

This design gives up borrowed return values, stored references, arbitrary
zero-copy APIs, and direct cyclic object graphs. Those limitations are the
source of much of its simplicity. Real example programs should be written
before relaxing any of them.

## References

- [Oxide: The Essence of Rust][oxide]
- [RustBelt: Securing the Foundations of Rust][rustbelt]
- [Rust non-lexical lifetimes and Polonius][nll]
- [Austral specification][austral]
- [Austral linear types][austral-linear]
- [Native Implementation of Mutable Value Semantics][mvs]
- [Hylo introduction][hylo]
- [Perceus: Garbage-Free Reference Counting with Reuse][perceus]
- [Fully-Automatic Type Inference for Borrows with Lifetimes][auto-borrow]
- [Swift Automatic Reference Counting][swift-arc]
- [Lean 4 runtime memory representation][lean-runtime]
- [Region-Based Memory Management in Cyclone][cyclone]
- [Project Verona: Reference Capabilities for Flexible Memory Management][verona]
- [Immix: A Mark-Region Garbage Collector][immix]
- [Distilling the Real Cost of Production Garbage Collectors][gc-cost]
- [Boehm-Demers-Weiser conservative garbage collector][boehm]

[oxide]: https://arxiv.org/abs/1903.00982
[rustbelt]: https://people.mpi-sws.org/~dreyer/papers/rustbelt/paper.pdf
[nll]: https://blog.rust-lang.org/2022/08/05/nll-by-default/
[austral]: https://austral-lang.org/spec/spec.html
[austral-linear]: https://austral-lang.org/tutorial/linear-types
[mvs]: https://arxiv.org/abs/2106.12678
[hylo]: https://hylo-lang.org/introduction/
[perceus]: https://doi.org/10.1145/3453483.3454032
[auto-borrow]: https://doi.org/10.1145/3798221
[swift-arc]: https://docs.swift.org/swift-book/documentation/the-swift-programming-language/automaticreferencecounting/
[lean-runtime]: https://github.com/leanprover/lean4/blob/master/src/include/lean/lean.h
[cyclone]: https://www.cs.cornell.edu/projects/cyclone/papers/cyclone-regions.pdf
[verona]: https://www.microsoft.com/en-us/research/publication/reference-capabilities-for-flexible-memory-management/
[immix]: https://openresearch-repository.anu.edu.au/items/32c6080b-51ee-433e-981d-e5960787a3fb
[gc-cost]: https://arxiv.org/abs/2112.07880
[boehm]: https://www.cs.princeton.edu/~appel/modern/c/software/boehm/gc.html

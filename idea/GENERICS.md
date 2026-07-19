# IDEA: Minimal Generics with Bounded Compilation Cost

Status: parametric Core selected and implemented for the initial unconstrained
Source v1 profile; constraints, witnesses, specialization, and final
representation policy remain exploratory.

SlopH should support parametric generic types and functions in the first stable
language. Reusable data structures alone require this facility: collections,
`Option`, `Result`, tuples, tasks, iterators, and user-defined containers must
not need a separately copied declaration for every element type.

The broader design assumes generic parameters, explicit type arguments in Core,
and separately compiled generic code. Core v0 remains monomorphic; the
authoritative C11 compiler implements the initial design in Core v2.

Generics are admitted only with bounded, observable compilation work. The
default compiler must not recursively monomorphize an entire dependency graph
or rely on whole-program specialization to produce acceptable programs.

## Minimal Source Model

The initial surface language should support generic nominal types and generic
functions:

```text
type List[Item] {
    Nil();
    Cons(head: Item, tail: List[Item]);
}

fn identity[Item](value: Item) -> Item {
    value
}

fn map[Input, Output](items: List[Input], transform: fn(Input) -> Output) -> List[Output] {
    ...
}
```

Generic parameters are in scope only within their declaration. Public
declarations explicitly list their generic parameters. Every generic type,
function call, and constructor call supplies all type arguments explicitly.
The initial profile performs no type-argument inference. Case patterns omit
type arguments because the scrutinee fixes the enum instantiation.

Generic code that performs an operation on an otherwise unknown type uses an
explicit interface or trait constraint:

```text
fn contains[T: Equal](items: List[T], wanted: T) Bool {
    ...
}

type Map[K: Equal + Hash, V] {
    ...
}
```

The spelling is provisional. Semantically, a constraint declares exactly which
operations the generic implementation may use. The generic body is checked
once against those constraints rather than re-typechecked independently for
every concrete type argument.

The initial design should include:

- generic enums and their single-constructor struct form;
- generic functions;
- recursive uses such as `List[T]` within their ordinary recursion rules;
- constrained generic parameters using the selected coherent interface model
  after the unconstrained profile is stable;
- explicit generic parameters and constraints in compiled module interfaces;
- an explicit validated compiler representation of type abstraction and
  application, retained in canonical typed Core only if the parametric-Core
  design is selected.

The initial design should exclude:

- higher-kinded parameters such as an abstract `F[T]`;
- GADTs, type families, dependent types, and general type-level computation;
- variadic generic parameter lists;
- user-defined specialization rules;
- overload resolution based on competing overlapping implementations;
- variance or generic subtyping rules;
- general constant generics.

These exclusions can be reconsidered only through concrete use cases and the
top-level admission rule.

## Can Generics Stay Outside Core?

Generics do not necessarily require generic forms in canonical Core. The
project should prototype and measure three lowering boundaries before making
Core polymorphism permanent.

### Parametric Core

The direct design retains type abstraction, type application, generic nominal
declarations, and constraints in typed Core. A generic body is independently
validated once, and later backend lowering supplies witnesses, representation
metadata, or selective specializations.

This is the selected initial direction because it preserves generic module
interfaces, separate compilation, and independent Core validation without
requiring a concrete instantiation. Its cost is a larger normative Core type
system and validator.

### Pre-Core Monomorphization

The surface type checker can instantiate every used generic declaration into
monomorphic declarations before emitting Core. Canonical Core then needs no
type abstraction or application.

This keeps Core smaller and can generate efficient concrete layouts, but it
makes compilation work depend on the transitive set of type applications. It
also complicates separate compilation, code-size control, recursive generic
instantiation, cache identity, and generic library artifacts. It is acceptable
only if bounded performance tests demonstrate that it does not recreate the
compile-time failure mode this project explicitly rejects. Mandatory
whole-graph monomorphization is not the default assumption.

### Pre-Core Type Erasure

The surface type checker can instead lower a checked generic body into one
monomorphic Core function over a uniform erased value representation. Explicit
dictionaries and trusted type metadata provide required operations such as
comparison, layout, movement, and destruction.

This permits separately compiled generic implementations without generic Core,
but may introduce boxing, indirect calls, larger values, or runtime checks. The
erased representation, metadata authenticity, ownership behavior, and trusted
conversion primitives become part of Core or runtime semantics even though
generic syntax does not. Erasure therefore moves complexity; it does not make
the semantic problem disappear.

### Library Boundary

Collections and generic algorithms should be libraries, but parametric type
checking itself cannot be an ordinary library feature. The compiler must know
that `List[Int]` and `List[Text]` are distinct well-formed types, substitute
field types, check constraints, resolve applications, and preserve ownership
and representation rules before library code can run.

A macro library could generate one named container per requested type, but that
is code generation rather than generics. It produces worse module identities,
diagnostics, caching, interoperability, and compile-time scaling and cannot
serve as the standard semantic model. A library may provide generic syntax and
convenience transformations only over a compiler-defined generic type system.

The admission decision is therefore not simply "Core or library." It is:

```text
surface generic type system in the compiler
        |
        +-- parametric typed Core
        +-- bounded pre-Core monomorphization
        +-- checked pre-Core erasure with witnesses
```

All three strategies must expose the same source semantics. The chosen default
must be supported by the compile-time and runtime measurements below. A
non-Core implementation is preferred if it preserves independent validation,
separate compilation, predictable performance, ownership safety, and useful
runtime representation without adding a larger trusted boundary elsewhere.

## Core and Separate Compilation

Under the parametric-Core design, typed Core retains generic type and term
abstraction and application. A constructor application names its nominal
constructor and supplies explicit type arguments. The Core validator checks
parameter arity, kind, constraints, field substitution, ownership, effects,
and application types without relying on surface inference.

Under either pre-Core design, the surface checker performs those checks and
emits only validated monomorphic or erased declarations. The Core validator
must still verify every resulting declaration, witness, metadata value, and
trusted conversion without trusting an unvalidated surface annotation.

A compiled module interface records:

- generic parameter order and category;
- parameter constraints and required witnesses;
- the fully checked public signature;
- ownership, effect, representation, and ABI facts required by callers;
- a semantic hash that is independent of private implementation details.

The default implementation compiles a generic body separately. Required
interface operations may elaborate to explicit dictionary or witness
arguments. When representation-independent code needs size, alignment, move,
copy, or destruction behavior, the implementation may pass bounded
compiler-generated type metadata or representation witnesses.

The exact runtime representation remains coupled to the eventual memory model,
but it must preserve these properties:

- checking a generic body does not require enumerating its future uses;
- importing a generic library does not require its source implementation;
- adding an unused concrete type creates no generic instantiations;
- a private body change with an unchanged interface does not re-typecheck
  consumers;
- generic recursion cannot create an unbounded compile-time instantiation
  chain;
- compiled artifacts record every representation or witness assumption needed
  for validation and cache identity.

Selective specialization is permitted as a bounded optimization. It must be
semantics-preserving, observable in compiler statistics, independently
cacheable, and subject to explicit limits. The ordinary development profile
must remain usable with specialization disabled. Cross-module or whole-program
specialization belongs to an optional optimization profile.

> TODO before permanently freezing parametric Core: prototype and measure
> checked pre-Core erasure and bounded pre-Core monomorphization against the
> same correctness, compile-time, artifact-size, and runtime corpus. The
> bootstrap currently validates parametric Core and then erases type binders
> and applications for its uniform boxed C runtime; this implementation choice
> is not evidence that every future backend must use the same representation.

Generic body discovery, application instantiation, and helper emission should
also follow the general
[reachability-driven compilation](./REACHABILITY_COMPILATION.md) policy. Merely
constructing a generic data type does not make all of its methods reachable.

## Required Work Limits and Diagnostics

The compiler must count and expose at least:

- generic declarations checked;
- generic applications resolved;
- constraint and witness lookups;
- concrete layout instantiations;
- specialized function bodies requested, reused, emitted, and rejected by a
  limit;
- time and peak memory for generic checking, witness elaboration, layout, and
  specialization;
- artifact bytes attributable to generic metadata and specializations.

Explicit positive limits bound generic type nesting, constraint depth, witness
search, layout instantiations, specializations per declaration, total
specializations per build, and generated artifact size. Exceeding a limit
produces a deterministic diagnostic containing the responsible declaration,
type-argument chain, configured limit, and actionable alternatives. The
compiler must not hang, exhaust ambient memory, or silently switch to an
unbounded strategy.

## Compile-Time Performance Corpus

The toolchain performance corpus must contain generated but structurally
realistic generic packages. Generators, seeds, parameters, and their canonical
output hashes are checked in or versioned so results remain reproducible. The
corpus must not consist only of repeated identity functions.

At minimum it contains these workloads:

1. **Generic collection library.** Separately compiled `List`, vector, map,
   set, iterator, `Option`, and `Result` packages with realistic constraints,
   higher-order calls, error paths, and public interfaces.
2. **Instantiation breadth.** The same generic types and functions are used by
   1, 10, 100, and 1,000 distinct nominal element types. This detects repeated
   body checking and accidental quadratic lookup or code generation.
3. **Type nesting.** Valid generic types are nested to depths 1, 8, and 32, plus
   one input immediately beyond the configured limit. The invalid case must
   fail within the same bounded parser and diagnostic envelope.
4. **Constraint load.** Generic algorithms use 0, 1, 4, and 16 independently
   required witnesses across many modules. Implementations remain coherent and
   contain no intentionally ambiguous lookup.
5. **Dependency fanout.** One generic library is consumed by at least 100
   modules across multiple packages. Consumers use different type arguments so
   interface loading, cache reuse, and invalidation are exercised.
6. **Recursive pressure.** Legal recursive containers and mutually recursive
   generic calls are combined with an illegal expanding instantiation chain.
   The legal case compiles; the expanding case reaches a deterministic limit
   without process or host stack exhaustion.
7. **Concurrent worktrees.** At least four clean builds of the generic reference
   workload run concurrently with isolated build directories and an empty
   shared cache.

Every workload has a comparable non-generic or specialization-disabled control
where one can be constructed without changing the program's work. Controls
are used to attribute generic overhead rather than to claim that unlike
programs should compile in identical time.

## Measurement Procedure

Generic compile-time measurements must cover both `check` and native `build` in
the default development profile. Optional optimizing profiles are reported
separately and may not replace default-profile measurements.

For every release candidate and every change to generic checking, constraint
resolution, Core elaboration, layout, specialization, interfaces, or caching:

1. record the compiler revision, language and Core versions, target, reference
   machine, operating system, worker count, and workload content hash;
2. run a source-only cold build with local and remote artifacts disabled and an
   empty compiler cache;
3. run a warm no-change build;
4. edit one consumer implementation without changing its interface and build;
5. edit the generic library's private implementation without changing its
   interface and build;
6. change one public generic constraint or signature deep in the dependency
   graph and build;
7. add one new concrete use in one leaf module and build;
8. repeat the cold build in four concurrent isolated worktrees;
9. repeat applicable cases with specialization disabled and with the bounded
   release optimization profile enabled.

Before selecting the permanent Core boundary, run the same corpus through
parametric-Core, bounded-monomorphized, and erased prototypes where feasible.
Report compile time, peak memory, artifact size, clean and incremental
invalidation, executable size, allocation count, indirect-call count, and
runtime throughput. A smaller Core is not a win if its lowering strategy causes
unbounded builds or makes general-purpose data structures disproportionately
slow.

Each case records wall-clock time, total CPU time, peak resident memory, modules
parsed and checked, interfaces loaded and rewritten, Core bodies validated,
layout instantiations, emitted specializations, cache hits and misses, generated
code size, and critical dependency path. Report at least five measured runs
after one unmeasured process warm-up when measuring warm operating-system
conditions. Publish the median and slowest measured run; cold-cache results
must not be mixed with warm-cache results.

Dedicated reference-machine runs provide timing gates. Ordinary CI also checks
structural invariants that are less sensitive to noise: module invalidation
sets, instantiation counts, specialization counts, cache decisions, artifact
identity, and deterministic output across worker counts and checkout paths.

## Performance Acceptance Rules

Before v1, the project must publish absolute wall-time and peak-memory budgets
for the generic corpus on the reference machine as part of the complete
200,000-line, 150-dependency workload. A generic implementation is not stable
until those budgets exist and pass from an empty cache.

The following relative rules apply even before absolute budgets are selected:

- `check` validates each generic declaration body once per semantic interface,
  not once per concrete application;
- work and memory for the breadth series must grow approximately linearly with
  declared uses and required layouts, never with the Cartesian product of all
  generic types visible in the graph;
- doubling the largest breadth workload may not exceed 2.5 times the generic-
  attributable time, peak memory, layout count, or emitted specialization
  count without a documented investigation and explicit acceptance;
- a warm no-change build performs no generic body rechecking, new layout work,
  or specialization code generation;
- a leaf-only new instantiation rebuilds that leaf and genuinely dependent
  artifacts, not unrelated users of the same generic declaration;
- a private generic-library implementation change with an unchanged interface
  does not re-typecheck consumers in the default separately compiled profile;
- specialization-disabled builds must compile every valid corpus program and
  remain within the default development-profile budget;
- concurrent worktree builds must meet the project's aggregate peak-memory and
  feedback-latency budgets without correctness depending on a shared cache.

A regression report identifies which phase, declaration, dependency path, or
specialization caused the change. A generic feature may not be accepted based
only on a small microbenchmark, a warm cache, or runtime speed gained through
unbounded compilation.

## Questions to Resolve

- Whether the implemented generic declaration and explicit type-application
  syntax should be frozen unchanged.
- Whether permanent typed Core remains parametric after the required erasure
  and bounded-monomorphization measurements.
- The small kind system needed to distinguish ordinary types and any permitted
  representation categories.
- The coherent interface, implementation, and witness-selection rules.
- Which representation metadata is required by the selected memory model.
- Whether any fixed-size array use case justifies a narrowly bounded constant
  parameter facility.
- Absolute compile-time and memory budgets on the selected reference machine.

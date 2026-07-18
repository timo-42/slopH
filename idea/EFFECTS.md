# IDEA: Purity and a Small Function Effect System

Status: exploratory, non-normative.

SlopH should distinguish pure functions from functions with observable effects.
This can improve AI reasoning, optimization, compile-time evaluation, testing,
capability enforcement, and reachability-driven compilation. It should not
require Haskell's lazy evaluation, a universal `IO` monad, monad transformers,
or a general algebraic-effect language.

The initial direction should use a small finite effect set inferred from
function bodies and recorded in typed Core and compiled interfaces. Source
declarations may assert a stricter contract when API stability or compile-time
use requires it.

## Why Purity Is Useful

A pure function's behavior is determined only by its explicit arguments and
immutable language semantics. It cannot secretly read a clock, environment
variable, global mutable value, file, network connection, random source, or
other ambient state, and it cannot produce an externally visible side effect.

Knowing this permits the compiler and tools to:

- remove an unused pure call or binding safely;
- reorder or evaluate pure expressions when ownership and trapping rules also
  permit it;
- memoize pure compile-time work by semantic inputs;
- run independent pure work in parallel;
- permit pure functions in macros and constant evaluation under resource
  limits;
- explain to an AI agent which calls can change external state;
- test deterministic functions without constructing an execution environment;
- reject undeclared I/O or nondeterminism in reproducible build phases.

Purity is not the same as termination, totality, allocation freedom, or absence
of failure. A pure computation may allocate, recurse, consume large resources,
or return `Result`. Trapping and divergence affect which transformations are
valid and must not be hidden by the word `pure`.

## Relationship to Haskell

Haskell obtains a strong separation by representing effectful computations in
types such as `IO a`. Ordinary pure functions cannot execute an `IO` action.
This has substantial reasoning and testing value.

SlopH should retain the enforceable separation while using a model suited to a
strict general-purpose language and a small compiler:

- ordinary calls remain ordinary calls;
- effects are finite properties of function types rather than values wrapped
  in a universal effect container;
- explicit capability values determine authority to access external services;
- `Result` represents recoverable failure independently from I/O;
- suspension is one effect dimension and does not imply I/O;
- v1 has no general user-defined effect handlers or monad transformers.

This still introduces function colors, but the colors form one small
compiler-defined set with deterministic inclusion rules. The benefit is
worthwhile only if higher-order propagation remains simple and inference stays
bounded.

## Proposed Effect Set

The first implementation should investigate a finite bit set conceptually
similar to:

```text
pure          no observable effect
mutate        mutates caller-observable state
io            interacts with an external service
nondeterministic
              depends on clock, randomness, scheduling, or ambient state
may_suspend   may suspend and later resume
may_trap      may terminate the computation without returning
unsafe        uses a trusted or foreign operation outside ordinary guarantees
```

Names and grouping remain provisional. Each admitted effect needs one precise
semantic meaning and optimization rule.

Allocation and mutation of fresh locally owned storage need not make a function
externally impure when neither identity nor intermediate mutation is observable.
Mutation through an argument, global state, shared cell, or externally visible
capability is observable and carries the appropriate effect.

`nondeterministic` may be distinct from `io`: content-addressed input can be
deterministic despite physical I/O, while clock or randomness access is
nondeterministic without changing external state. V1 should keep the
distinction only if concrete use cases justify its cost.

## Source Model

Effects should normally be inferred for directly declared functions:

```text
fn add(left: Int, right: Int) Int {
    left + right
}

fn read_config(fs: FileSystem, path: Path) Result[Config, Error] {
    bytes = fs.read(path)
    return parse_config(bytes)
}
```

Conceptually, `add` is pure while `read_config` carries the effects of
`FileSystem.read` and `parse_config`. The caller needs no special call syntax.

A declaration may assert a contract:

```text
pure fn normalize(value: Text) Text {
    ...
}
```

The compiler proves that the inferred effects satisfy the assertion. Exact
syntax is deferred. Assertions are useful for public APIs, compile-time
functions, reproducible build logic, and deterministic tests; they are not
unchecked promises.

An exported function's effect contract is recorded in its compiled interface.
Changing a public function from pure to effectful is an interface change even
when ordinary parameter and result types remain unchanged. Tooling should warn
before an inferred public effect change is accepted and suggest an explicit
contract when stability matters.

## Function Types and Higher-Order Code

Effects are part of function types because a caller must know what invoking a
received function can do. Conceptually:

```text
fn(Int) -> Text effects {pure}
fn(Request) -> Response effects {io, may_suspend}
```

A pure function can be used where a caller permits a superset of its effects.
An effectful function cannot be passed where only a pure callback is permitted.

Higher-order functions need bounded effect polymorphism so APIs do not split
into `pure_map`, `io_map`, and `async_map` variants:

```text
fn map[A, B, effects E](
    items: List[A],
    transform: fn(A) B effects E,
) List[B] effects E
```

The syntax is illustrative. The supported operation is finite set propagation,
not arbitrary row types or user-defined effect algebra. Inference must reduce
to bounded union and subset constraints with deterministic diagnostics.

An unknown external or indirect call conservatively receives every effect it
may have unless a validated interface proves a smaller set. Foreign functions
require explicit effects checked against their trusted wrapper policy.

## Capabilities and Effects

Effects describe behavior; capabilities grant authority. Neither replaces the
other.

```text
fn read_config(fs: FileSystem, path: Path) Result[Config, Error]
```

The `FileSystem` argument makes authority explicit and testable. The `io`
effect prevents callers and optimizers from treating the operation as pure.
Code without an appropriate capability cannot perform the operation even if
its function type permits `io`.

Filesystem, network, clock, randomness, environment, secrets, and process
creation should not be ambient globals. Program startup supplies explicit
capabilities, and package interfaces record the resulting effects.

These capability boundaries also support the deterministic interceptors and
fault plans proposed by the
[bundled test framework](./TEST_FRAMEWORK.md), without requiring global mocks.

## Core and Validation

Canonical typed Core records resolved effects on function types, declarations,
calls, primitives, and foreign operations. The independent validator checks:

- a function body uses no effect outside its contract;
- calls propagate the callee's effects;
- higher-order calls obey effect subset constraints;
- pure code cannot invoke an effectful primitive or forge a capability;
- elimination and reordering preserve traps, mutation, destruction, and
  external behavior;
- trusted operations carry valid non-forgeable provenance.

Effects do not require a new Core expression constructor. They are properties
of types, primitives, declarations, and validation judgments. The complete
finite catalog and its operational rules count toward Core's semantic size.

## Inference and Separate Compilation

Direct-call inference is a monotone fixed point over a finite bit set. Each
primitive begins with cataloged effects. Each function accumulates its local
effects and those of its callees. Recursive strongly connected components are
processed together until no bit changes.

Because the catalog is finite and effects only accumulate, inference has a
small deterministic bound. It must not use heuristic repeated type checking or
whole-program dependency implementation loading.

Public interfaces contain effect contracts. Within one package, changing a
private body invalidates inference only for reverse call-graph dependents. A
public effect change invalidates consumers like a public type change.

Effect-polymorphic functions expose finite variables and subset constraints in
their interfaces. V1 should reject rules requiring general constraint solving,
ambiguous minimum effects, or enumeration of effect-set combinations.

## Reachability and Dead Code

An effectful declaration is not automatically a reachability root. Only a
reachable call, explicit initialization, export, registration, or other root
makes its behavior part of the selected program.

Within reachable code:

- an unused pure expression may be removed when traps and resource semantics
  permit it;
- an unused effectful call remains because its effect is observable;
- destruction remains when an owned value has an effectful destructor;
- helpers used only by unreachable functions are not analyzed or emitted;
- explicit initializers create visible edges rather than running because a
  module was imported.

This improves the precision of
[reachability-driven compilation](./REACHABILITY_COMPILATION.md) without making
side effects themselves roots.

## Async

Suspension is independent from purity. A deterministic computation may suspend,
while an effectful blocking foreign call may never suspend.

The caller-controlled async direction can treat `may_suspend` as one inferred
bit recorded in function values and interfaces. Ordinary calls propagate it.
Higher-order effect propagation prevents separate synchronous and asynchronous
versions of every collection algorithm.

## AI-First Diagnostics

Canonical signatures and inspection output show inferred effects even when
source omits them. An AI agent must be able to ask why a function has an effect
and receive a deterministic call chain:

```text
load_settings
  -> read_config
  -> FileSystem.read
  -> primitive fs.read [io]
```

Violation diagnostics include the asserted contract, inferred effect, callee
or primitive introducing it, relevant spans, and actionable repair options.
The language should avoid decorative annotations at every call site while
keeping effects visible in public interfaces and canonical typed output.

## Compile-Time Performance Tests

The performance corpus tests effect inference independently and together with
generics, async, packages, and reachability. It contains at least:

1. call chains of lengths 1, 10, 100, 1,000, and the configured maximum;
2. recursive components containing 1, 10, 100, and 1,000 functions;
3. fanout and fan-in graphs where one leaf introduces one effect bit;
4. higher-order functions with 1, 10, and 100 finite effect constraints across
   separate modules;
5. indirect calls with exact, bounded, and unknown effect sets;
6. a private effect change that preserves a public contract and one that
   changes a public interface;
7. selected graphs in which large effectful subgraphs remain unreachable;
8. four concurrent source-only builds in isolated worktrees and empty caches.

For clean, warm no-change, private-body change, public-effect change, and deep
dependency invalidation cases, record wall time, CPU time, peak memory,
functions and edges visited, strongly connected components, fixed-point bit
additions, changed interface hashes, invalidated modules, cache results, and
diagnostics.

Structural CI assertions verify that:

- each concrete effect bit is added to a function at most once per run;
- recursive inference terminates within a bound derived from declarations and
  the finite catalog;
- unreachable subgraphs do not participate in selected-build inference;
- complete-package checking visits every declaration;
- a private change with an unchanged contract does not invalidate consumers;
- results and diagnostic order are identical across worker counts and paths;
- higher-order propagation does not enumerate all effect subsets;
- cached and empty-cache builds produce identical interfaces.

Before v1, absolute clean and incremental budgets must be defined on the
reference machine. Until then, doubling the largest ordinary call graph may not
exceed 2.5 times inference time, peak memory, or visited edges without a
documented investigation and explicit acceptance. Inference must remain a
small reported fraction of check latency.

## Admission Rule

The initial system is accepted only if it:

- separates pure and effectful code strongly enough for optimization,
  reproducible compile-time execution, and capability enforcement;
- avoids fragmenting higher-order APIs by effect color;
- keeps inference and diagnostics deterministic and bounded;
- avoids pervasive ceremonial wrappers in ordinary application code;
- supports separate compilation through compact interfaces;
- keeps its complete catalog and rules within the AI context budget.

If the multi-effect design fails these criteria, v1 should use a smaller
distinction such as `pure`, `effectful`, and the independently required
`may_suspend` bit rather than adding a general effect language.

## Questions to Resolve

- Whether `io` and `nondeterministic` justify separate v1 effects.
- Whether trapping belongs in function types or only optimization metadata.
- Which mutations of uniquely owned values remain externally pure.
- How destructor and cancellation effects appear in public types.
- Whether public source declarations infer effects or require an explicit
  upper-bound contract.
- The exact finite effect-polymorphism syntax for higher-order functions.
- The conservative effect assigned to unknown foreign or dynamically loaded
  calls.
- Absolute inference time and memory budgets on the reference machine.

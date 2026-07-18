# IDEA: Policy-Selected Collection Interfaces

Status: exploratory, non-normative.

SlopH's official collection packages should let code request the behavior it
needs without forcing every caller to name one storage representation. A
compile-time policy selects an array, vector, bounded vector, deque, arena-backed
sequence, or another compatible implementation. Users and third-party packages
may provide alternative implementations through the same public interfaces.

The application retains final control. A command-line or embedded application
must be able to require static or bounded allocation for its entire reachable
program even when its libraries were written against general collection
interfaces.

This is a library design built on language-defined generics, interfaces,
compile-time values, effect inference, and reachability checking. Collections
do not belong in the mandatory `core` library merely because the compiler
provides the mechanisms required to express them.

## Goals

- Let libraries state the weakest collection capabilities they require.
- Let applications select official or user-defined implementations.
- Select implementations deterministically from explicit requirements.
- Support fixed, bounded, initialization-only, and general allocation models.
- Diagnose every reachable operation that violates an application memory
  profile, including violations hidden in dependencies or foreign calls.
- Keep unused collection implementations outside parsing, checking,
  specialization, code generation, and linking.
- Preserve separate compilation and bounded generic compilation work.
- Make representation, ABI, cache, and failure behavior explicit.

## Non-Goals

- One universal interface covering every sequence, map, set, and queue.
- Heuristic selection based on undocumented compiler performance guesses.
- Silently replacing one representation with another when observable behavior
  differs.
- Treating capacity exhaustion as impossible or silently falling back to the
  heap.
- Claiming that static collection selection alone proves an application free of
  dynamic allocation.
- Adding unrestricted dependent types or general type-level programming only
  to configure collections.

## Names and Semantic Families

`Array` and `Vector` conventionally imply particular representations. The
general owning indexed-sequence family should therefore use the provisional
name `Seq`:

```text
collections.Seq[T, requirements]
```

Common names remain available as aliases for common requirement sets:

```text
Array[T, N]          fixed length and contiguous storage
Vector[T]            dynamically growing contiguous sequence
BoundedVector[T, N]  contiguous sequence with maximum capacity N
SmallVector[T, N]    N inline elements, then an explicitly selected fallback
```

These aliases improve readability but do not receive special semantics merely
because their names are familiar.

Different semantic families remain separate:

```text
collections.Seq[T, requirements]
collections.Map[K, V, requirements]
collections.Set[T, requirements]
collections.Queue[T, requirements]
```

A sequence and a map have fundamentally different operations, invariants, and
selection criteria. A single `Container` abstraction would hide useful facts
and create an interface that no implementation can satisfy honestly.

## Requirements Rather Than Representation Names

A user may request an owning sequence with a typed compile-time requirements
record:

```text
type Messages = Seq[Message, {
    length: 0..=1024
    layout: contiguous
    address_stability: unstable
    storage: inline_then_pool(16)
    growth: bounded
    shrink: explicit
}]
```

The syntax is provisional. The requirements record must be closed, typed,
canonical, bounded in size, and valid without arbitrary compile-time I/O.

The design must distinguish semantic requirements from tuning preferences.

Semantic requirements may include:

- minimum and maximum logical length;
- fixed, contiguous, segmented, or unspecified layout;
- element-address and iterator invalidation guarantees;
- ordering guarantees;
- whether insertion can fail because capacity is exhausted;
- required random, front, back, or indexed access operations;
- ownership, sharing, and thread-safety properties;
- whether storage may be moved;
- whether the type is ABI-visible or opaque.

Tuning preferences may include:

- initial or inline capacity;
- geometric or linear growth preference;
- automatic, explicit, or disabled shrinking;
- expected append, insertion, lookup, and removal frequency;
- preferred allocator or pool;
- a size, latency, or memory optimization preference.

An implementation selector may use tuning preferences only among candidates
that satisfy every semantic requirement. A tuning preference cannot weaken
failure behavior, contiguity, address stability, iteration order, or another
observable contract.

`min` and `max` must be qualified names such as `length`, `capacity`, or
`allocation_bytes`. An unqualified `min` is ambiguous: minimum logical length,
initial capacity, inline capacity, and reserved memory are different concepts.

## Capability Interfaces

Libraries should depend on small capability interfaces rather than on the
complete `Seq` API. Provisional interfaces include:

```text
interface ReadSeq[T] {
    fn length(self) Int
    fn get(self, index: Int) Option[ref T]
}

interface MutableSeq[T]: ReadSeq[T] {
    fn set(mut self, index: Int, value: T) Result[Unit, BoundsError]
}

interface GrowableSeq[T]: MutableSeq[T] {
    fn push(mut self, value: T) Result[Unit, CapacityError]
    fn pop(mut self) Option[T]
}

interface ContiguousSeq[T]: ReadSeq[T] {
    fn elements(self) Slice[T]
}
```

An immutable array need not implement `GrowableSeq`. A deque may implement
`GrowableSeq` without claiming `ContiguousSeq`. A container whose elements move
on growth must not implement an address-stability interface.

A library can accept caller-owned storage directly:

```text
fn tokenize[S: GrowableSeq[Token]](
    input: Text,
    output: mut S,
) Result[Unit, TokenizeError] {
    ...
}
```

This form requires no implicit provider and gives the caller direct ownership
of capacity and failure handling.

Maps and sets similarly split readable lookup, mutable replacement, insertion,
ordered iteration, stable references, and other capabilities. Hash-based and
ordered maps must not be interchangeable when iteration order is observable.

## Providers and User Implementations

Construction convenience requires an explicit provider abstraction. A
provisional form is:

```text
interface SeqProvider[T, Requirements] {
    type Storage: ReadSeq[T]

    compile fn supports() Bool
    fn create() Result[Storage, CreateError]
}
```

The exact associated-type design depends on the minimal generics proposal. If
generic associated types are unavailable, the API can instead pass a concrete
storage type and a `Factory[Storage]` witness. The design may not quietly add
higher-kinded types merely to make provider syntax shorter.

Official implementations might include:

```text
FixedArrayProvider
HeapVectorProvider
StaticVectorProvider
PoolVectorProvider
DequeProvider
ArenaSequenceProvider
```

Third-party packages may implement the same public interfaces and advertise
their supported requirements. Provider implementations are ordinary packages,
not compiler plugins.

Provider selection is application-controlled and coherent. A dependency may
state requirements, but it may not install a process-global implementation or
override the root application's memory policy. The root selects an explicit
ordered provider set or a named collection environment:

```text
application {
    collections: StaticCollections
}
```

Conceptually, library construction can use that selected environment:

```text
fn parse[using C: Collections](input: Text) {
    let tokens = C.seq[Token]({
        length: 0..=4096
        layout: contiguous
        growth: bounded
    })
}
```

Implicit-provider syntax is optional. Its elaborated Core and compiled
interface must contain explicit provider witnesses so behavior never depends on
ambient registry order, import order, or linker symbol order.

## Deterministic Selection

The selector filters candidates by semantic requirements, target support,
memory profile, and required interfaces. It then applies explicit application
priority. It must not benchmark during a build or choose a candidate using an
undocumented cost model.

A conceptual selection report is:

```text
collection selection: Messages
  family: Seq[Message]
  provider: StaticCollections
  representation: InlinePoolVector
  inline capacity: 16
  maximum length: 1024
  fallback: preallocated pool
  heap allocation after initialization: impossible
```

If zero candidates satisfy the request, compilation fails with the unsatisfied
requirements and the rejection reason for each considered provider. If
multiple candidates have equal explicit priority, selection is ambiguous and
fails rather than depending on discovery order.

The selected provider identity, provider version, canonical requirements,
target facts, and ABI-relevant representation enter artifact cache identity.

## Application Memory Profiles

Selecting static collection providers does not prove that the program avoids
dynamic allocation. Allocation can also occur through strings, formatting,
closures, async frames, tasks, shared ownership, diagnostics, foreign
libraries, and direct allocation primitives.

Allocation must therefore participate in the inferred effect system. A
provisional effect distinction is:

```text
heap_reserve     request memory from the host heap or operating system
pool_claim       claim storage from an already reserved bounded pool
pool_release     return storage to a bounded pool
```

The final catalog should remain as small as possible, but it must distinguish
the policies the compiler promises to enforce.

Applications select a named build profile in their manifest. The CLI may
select that profile, but a transient flag must not silently weaken a locked
release policy:

```text
[profile.flight]
memory = "no-heap-after-init"
collections = "static"

[profile.static]
memory = "static-only"
collections = "static"
```

Possible profiles are:

| Profile | Initialization | Runtime |
| --- | --- | --- |
| `general` | Host heap allowed | Host heap allowed |
| `no-heap-after-init` | Host reservation allowed | Only pre-reserved bounded storage |
| `bounded-memory` | Declared pools reserved | Bounded pool claims and releases allowed |
| `static-only` | Static and bounded stack storage only | Static and bounded stack storage only |

`no-heap-after-init` and `bounded-memory` are intentionally distinct from
`static-only`. A pool can provide deterministic bounded storage while still
performing logical allocation and reuse at runtime.

These guarantees can also appear as requirement instances in the general
[audit-profile system](../AUDIT_PROFILES.md). For example, an organization's
`flight` audit profile may require `memory.profile = static-only`, bounded
collection capacities, and no unresolved provider obligations. The memory
profile controls compilation; the audit profile collects that compiler proof
with its other required evidence. An audit setting cannot weaken the selected
build memory profile.

The initialization boundary must be a language/toolchain concept, not a
function whose name happens to be `init`. The compiler records the permitted
initialization roots, proves that normal runtime cannot re-enter them, and
checks that all initialization failures are handled before runtime begins.

## Whole-Program Enforcement

After generic resolution and provider selection, the compiler checks every
reachable runtime function against the selected memory profile. A forbidden
effect is a compilation error with a shortest or otherwise deterministic call
path:

```text
error: runtime heap reservation is forbidden by profile `flight`

application::main
  -> config::parse
  -> diagnostic::format
  -> text::Builder.grow
  -> memory::heap_reserve
```

The check covers:

- ordinary and generic function bodies;
- selected interface witnesses and provider constructors;
- compiler-generated closure, async, and task machinery;
- panic, error, logging, and formatting paths;
- runtime and standard-library helpers;
- reachable C and other foreign calls;
- target-specific implementations selected by conditional compilation.

Foreign functions declare allocation and other effects in their binding
metadata. An unknown foreign function cannot be called from a restricted
profile merely because its declaration omits an effect. It requires a checked
wrapper or an explicit trusted contract, and that trust decision appears in
build provenance.

Reachability is necessary but cannot excuse indirect or dynamically selected
code that may run. Every possible target admitted by the program's validated
dispatch model is included unless the compiler can prove a narrower set.

## Capacity and Failure Semantics

Bounded collections never fall back silently to the heap. Operations that may
exceed a bound expose that possibility in their signatures:

```text
fn push(mut self, value: T) Result[Unit, CapacityError]
fn insert(mut self, key: K, value: V) Result[Option[V], CapacityError]
```

Compile-time-known violations should be rejected at compile time. Runtime
capacity exhaustion returns a typed failure unless the calling API explicitly
chooses a checked trap or another declared policy.

Minimum logical length also requires defined behavior. A `pop` that would
cross the minimum cannot be exposed as an infallible operation. Fixed-length
arrays should generally omit length-changing interfaces rather than return an
error for every call.

Shrinking has at least three meanings and must not be conflated:

- reducing logical length;
- returning slots to a preallocated pool;
- returning storage to the host heap or operating system.

Only the final operation violates a no-host-allocation runtime profile, while
the other two may still affect address stability and bounded execution time.

## Public Types, ABI, and Separate Compilation

If a selected representation is visible in a public type's layout, its
provider and canonical requirements are part of type and ABI identity. A
consumer cannot independently select a different representation for the same
transparent exported type.

An opaque collection wrapper may preserve its source-level identity while its
private representation evolves, provided its published capacity, invalidation,
failure, ownership, effect, and performance-class contracts remain satisfied.
Opaque representation does not permit lying about observable semantics.

Compiled interfaces record:

- required collection capabilities;
- provider witnesses where selection has already occurred;
- unresolved provider requirements where selection belongs to the consumer;
- allocation and failure effects;
- ABI-relevant representation and layout facts;
- semantic hashes independent of private implementation details.

Generic collection bodies follow the bounded separate-compilation policy in
[Minimal Generics with Bounded Compilation Cost](../GENERICS.md). Merely making
several providers available does not instantiate all of them. Only the selected
reachable implementation is loaded and emitted, following
[Reachability-Driven Compilation](../REACHABILITY_COMPILATION.md).

## Relationship to Compile-Time Language Features

The current generics idea deliberately excludes general constant generics and
general type-level computation. Collection requirements are a concrete use case
for a narrower mechanism, but they do not by themselves justify unrestricted
dependent types.

The implementation should prototype these options:

1. a small compiler-defined category of typed constant generic records;
2. a pure, bounded compile-time factory returning a type and explicit witness;
3. ordinary generic providers with a limited set of library-defined policy
   types rather than arbitrary numeric records.

All options must produce canonical interface data, deterministic diagnostics,
bounded evaluation, and identical source semantics. A macro that emits a new
unrelated nominal type for every use is not the preferred semantic model.

## Safety-Critical Motivation

Rule 3 of Gerard J. Holzmann's *Power of Ten* prohibits dynamic memory
allocation after initialization. Its rationale includes allocator timing,
memory exhaustion, fragmentation, leaks, dangling references, and the
difficulty of proving a fixed resource bound. The proposed
`no-heap-after-init` profile directly expresses that boundary, while
`static-only` provides a deliberately stricter option.

SlopH should not claim general NASA/JPL or safety-critical compliance merely
because it implements one memory rule. The other Power of Ten rules concern
control flow, loop bounds, function size, assertions, scope, checked results,
preprocessor use, pointer restrictions, warnings, and static analysis. A future
safety profile may explore those independently.

## Required Tests and Measurements

The conformance and benchmark corpus should include:

1. the same sequence algorithm using heap, fixed, pool, arena, and third-party
   providers;
2. deterministic selection under reordered imports, packages, and provider
   discovery;
3. exact-capacity success and one-element-over-capacity failure;
4. a dependency that performs hidden diagnostic-string allocation;
5. async, closure, formatting, task, and foreign-call allocation violations;
6. initialization-only reservation accepted before the phase boundary and
   rejected afterward;
7. a pool-backed program that remains within its declared byte and object
   bounds;
8. ambiguous and unsatisfied provider diagnostics;
9. opaque and transparent public types across separate compilation;
10. unused providers producing no checking, specialization, or emitted-code
    work.

Measurements include clean and incremental compile time, peak compiler memory,
provider and witness resolution counts, concrete layout count, generated code
size, runtime allocation counts, peak reserved and claimed bytes, capacity
failures, and operation latency distributions. Static collection claims must be
validated by instrumentation and backend inspection, not inferred solely from
type names.

## Questions to Resolve

- Whether `Seq` is sufficiently clear or `Sequence` is worth the longer name.
- The minimal capability-interface hierarchy for v1.
- Whether construction normally accepts caller-owned storage or uses an
  explicit provider environment.
- The exact constant-policy mechanism compatible with the restricted generics
  design.
- Whether allocation phases belong in the effect system, application manifest,
  or both.
- How stack bounds are computed when `static-only` permits bounded local
  arrays.
- Whether pool claim and release need separate public effects.
- How a library expresses conditional support for multiple memory profiles
  without compiling every implementation.
- Which representation facts are stable public contracts and which remain
  optimization choices.
- Whether official provider priority is standardized or always selected by the
  root application.

## References

- [The Power of Ten: Rules for Developing Safety-Critical Code][power-of-ten]
- [The Power of 10 overview][power-of-ten-wikipedia]
- [Minimal Generics with Bounded Compilation Cost](../GENERICS.md)
- [Inferred Effects and Explicit Public Contracts](../EFFECTS.md)
- [Reachability-Driven Compilation](../REACHABILITY_COMPILATION.md)
- [Layered Standard Library and Package Bundle](../STANDARD_LIBRARY.md)

[power-of-ten]: https://spinroot.com/gerard/pdf/P10.pdf
[power-of-ten-wikipedia]: https://en.wikipedia.org/wiki/The_Power_of_10:_Rules_for_Developing_Safety-Critical_Code

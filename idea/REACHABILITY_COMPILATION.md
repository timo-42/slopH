# IDEA: Reachability-Driven Compilation

Status: exploratory, non-normative.

SlopH should explore a Zig-inspired compilation model in which an ordinary
application build semantically analyzes and generates code only for
declarations reachable from the selected program roots. Complete validation of
every package declaration remains an explicit operation required by CI and
package publication rather than work repeated by every application build.

This policy applies to ordinary functions, methods, generic applications,
private helpers, trait or interface implementations, async frames, cleanup
functions, and compiler-generated runtime support.

Test declarations and fixtures follow the same selection rules in the proposed
[bundled test framework](./TEST_FRAMEWORK.md): only selected tests and their
transitive support become ordinary test-build roots.

Whether a reachable expression may be removed or reordered depends on the
proposed [purity and function effect system](./EFFECTS.md). An effectful
declaration is not automatically a root, but a reachable effectful call cannot
be discarded merely because its result is unused.

The goal is:

```text
selected roots
      |
      v
reachable declarations
      |
      v
required generic applications and runtime support
      |
      v
optimization, lowering, and machine-code generation
```

If a program constructs `Vector[Int]` but never calls `Vector.size`, the normal
build should not analyze or generate `Vector.size[Int]`. A private helper used
only by that method remains unreachable as well.

## Two Validity Contracts

SlopH should distinguish two useful meanings of a valid build.

**Selected-program validity** means that every declaration reachable from a
specific executable, test set, or other root is valid for the selected target
and features. Unreachable declarations do not affect this result.

**Complete-package validity** means that every declaration offered by a package
is semantically valid for the selected target and feature configuration,
whether or not the current program calls it.

An unreachable private function containing an error is not a problem for the
selected executable. If a later change calls it, that change discovers the
error and must repair or remove the function. This is comparable to selecting
a previously unused feature or target branch.

A published public function makes a stronger promise. Package authors should
not advertise an operation that has never passed semantic checking. Complete
package validation therefore remains mandatory before publication and for
compiler-shipped libraries.

Tests establish behavioral confidence only for the paths they exercise. They
do not replace complete static validation of a published API. Conversely,
complete static validation does not establish that a declaration behaves
correctly, so publication requires both complete checking and the package's
tests.

## Command Profiles

The eventual CLI should expose the distinction directly. Conceptually:

```text
<lang> build APP
    check, optimize, and generate only the graph reachable from APP

<lang> run APP
    use the same selected-program graph as build

<lang> test [SELECTION]
    check and generate the graph reachable from the selected tests

<lang> check --reachable APP
    check the selected graph without native code generation

<lang> check --all
    check every declaration in the current package

<lang> package publish
    require check --all and the configured publication test suite
```

Exact syntax is deferred. A diagnostic and machine-readable build report must
state whether the result proves selected-program validity or complete-package
validity. Tools must not present a reachability-only success as though every
package declaration was checked.

Compiler-shipped libraries, official package releases, and registry publication
CI require complete-package validation for every supported target and feature
profile declared by their support policy. This need not mean testing the
Cartesian product of all optional features; the package declares a finite
validation matrix.

## Declaration Discovery

The compiler first discovers enough structure to build the initial graph. It
must read package metadata and module headers, resolve imports and exports, and
identify declaration names and signatures required by consumers without
eagerly processing every implementation body.

Each declaration has a stable resolved identity conceptually equivalent to:

```text
package :: module :: enclosing-type :: declaration
```

Methods are independent declarations rather than one indivisible compilation
unit attached to a type. For example:

```text
Vector.push
Vector.get
Vector.size
Vector.capacity
```

The source representation must permit the compiler to locate an individual
body without reparsing unrelated bodies. Possible implementations include a
cheap bounded structural scan that records body byte ranges or an ordinary
parse whose per-declaration syntax trees can be released and reloaded. The
choice is an implementation detail subject to measurement; correctness cannot
depend on ambient caches.

Syntax errors that prevent reliable declaration or body-boundary discovery are
ordinary parse failures even when they occur in apparently unreachable text.
Once boundaries are known, semantic analysis of an unreachable body may be
deferred.

## Reachability Roots

Reachability starts only from explicit roots, including as applicable:

- the selected executable entry point;
- selected test entry points;
- public symbols exported through a native or foreign ABI;
- explicitly selected package initialization functions;
- referenced global values and their initialization;
- constructed interface dictionaries or virtual tables;
- destructors and cleanup functions required by reachable owned values;
- async resume, cancellation, and frame-destruction functions;
- explicitly registered callbacks, handlers, serializers, or plugin entries;
- target runtime operations required by reachable lowered code.

Exported functions are roots when producing a reusable dynamic library because
unknown external callers may invoke them. They need not all become roots when
producing a closed executable that merely depends on the package.

SlopH should avoid implicit global constructors, lookup of arbitrary functions
by source name, unrestricted runtime reflection, and global implementation
registries. These features obscure reachability and force conservative
retention. Dynamic facilities should use explicit registration so the
registration creates inspectable graph edges.

## Demand-Driven Work Queue

The compiler maintains a deterministic deduplicated queue of required work.
Processing a declaration body may discover direct calls, constructors,
interface witnesses, generic applications, cleanup operations, and runtime
primitives, which are then added to the queue.

Conceptually:

```text
enqueue main

process main
    enqueue Vector.push[Int]

process Vector.push[Int]
    enqueue allocate(Vector[Int])
    enqueue move[Int]

Vector.size[Int] is never enqueued
```

Work identity includes every semantic input that can change the result. For a
generic application this includes at least the declaration identity, type
arguments, selected witnesses, language and Core versions, target, features,
and build profile.

Queue order and parallel worker scheduling may affect latency but not emitted
artifacts or diagnostic order. Recursive discovery is bounded by explicit
limits for graph nodes, graph depth, generic expansion, generated declarations,
and output size. Exceeding a limit produces a deterministic diagnostic showing
the discovery chain.

## Interfaces and Checked Bodies

A compiled package interface exposes signatures and semantic facts needed to
resolve consumers without loading every body. It includes public types,
generic parameters, constraints, effects, ownership facts, exported syntax,
and implementation witnesses required for selection.

Checked bodies or typed Core should be independently addressable by declaration
identity. A consumer loads a body when reachability, inlining, specialization,
or target lowering genuinely needs it. Merely importing the package or naming a
type must not load all of its method bodies.

Registry-provided checked bodies are optional validated accelerators. A
source-only build remains correct with an empty cache and no binary artifacts.
When source is the only available representation, the compiler performs the
same demand-driven analysis locally.

Complete-package checking can populate independently cached checked bodies so
later selected builds pay only interface validation, reachability, and cache
lookup for unchanged declarations.

## Generics and Specialization

Reachability and generic strategy are separate decisions. Under parametric
Core, a reachable generic body may be compiled once using witnesses or runtime
type metadata. Under bounded monomorphization, only reachable concrete
applications are instantiated. Under erasure, only reachable erased bodies and
witness operations are emitted.

Selective specialization is an optimization and never a reachability root. A
specialization is requested only after its generic declaration and concrete
application are reachable. Helpers reachable exclusively from an unused
specialization remain absent.

Constructing `Vector[Int]` may require its concrete layout and destructor. It
does not by itself require every method declared for `Vector[T]`.

## Invalidation and Artifacts

Public interfaces and declaration bodies have distinct semantic hashes.
Changing the private body of `Vector.size` invalidates its checked-body and
generated-code artifacts but does not change the type's interface or invalidate
unrelated methods.

The package content hash used for integrity and locking may still change. That
does not require discarding every internal compilation artifact when their
semantic inputs remain identical.

The backend emits independently removable functions or equivalent fine-grained
sections. Final linker dead-section elimination is a safety net for conservative
compiler reachability, not the primary discovery mechanism. Ordinary pruning
must not require whole-program optimization or LTO.

Build inspection reports:

- all roots and why each is a root;
- why each declaration became reachable;
- declarations skipped as unreachable;
- bodies loaded, parsed, checked, optimized, specialized, and emitted;
- conservative retention caused by dynamic dispatch, FFI, or target rules;
- cache hits, misses, and invalidation reasons;
- time and peak memory saved or spent in each compilation stage.

## AI-First Workflow

AI-generated changes often contain speculative helpers, superseded approaches,
or functions that are not connected to the selected program. Demand-driven
builds keep that unused code out of expensive checking, specialization,
optimization, and backend stages.

The compiler should diagnose unreachable private declarations during complete
package checking and offer deterministic removal suggestions. Projects may
promote this diagnostic to an error so generated debris does not accumulate.
Public declarations are not considered removable merely because one program or
test selection does not reach them.

Diagnostics produced when a previously unreachable declaration first becomes
reachable must identify both the error in that declaration and the reachability
path that caused it to be analyzed. This gives an agent enough local evidence
to repair the old body, remove it, or undo the new reference.

Fast selected builds and complete validation serve different AI loops:

- selected builds optimize rapid edit, check, run, and test feedback;
- complete checks establish a package-wide contract before merge or publish;
- independently cached bodies prevent complete validation from becoming
  repeated work after unrelated changes.

## Required Performance Tests

The performance corpus must compare eager complete-package compilation with
reachability-driven builds using the same program semantics. At minimum it
contains:

1. a data-structure package with 100 independently callable methods, of which
   selected programs reach 1, 10, 50, and 100;
2. private helper trees unique to each method, plus helpers shared by several
   methods;
3. generic variants used with 1, 10, 100, and 1,000 concrete type arguments;
4. direct calls, constructed interface dictionaries, explicit callbacks,
   destructors, async cleanup, and conservative FFI roots;
5. a 100-module fanout in which different leaves reach different subsets;
6. one malformed declaration boundary, one semantically invalid unreachable
   body, and one body that becomes invalid only when selected target or feature
   rules apply;
7. four concurrent clean builds in isolated worktrees with empty caches.

For `build`, `check --reachable`, and `check --all`, record cold and warm wall
time, CPU time, peak resident memory, source bytes parsed, declaration bodies
checked, Core bodies loaded and validated, generic applications, emitted
functions, optimizer inputs, object size, final executable size, cache results,
and critical dependency path.

The tests must verify these structural properties independently of noisy timing
measurements:

- unused methods and their exclusive helpers never enter semantic analysis,
  specialization, optimization, or code generation during a selected build;
- shared helpers enter the graph exactly once when any reachable caller needs
  them;
- `check --all` analyzes every declaration and finds errors hidden from a
  selected build;
- package publication rejects a package that fails complete checking;
- adding one call analyzes only the newly reachable transitive subgraph;
- removing the last call makes that subgraph unreachable on the next clean
  build and omits it from final output;
- cached and source-only builds choose the same graph and produce identical
  semantic output;
- worker count and filesystem location do not change reachability or
  diagnostics;
- linker pruning never removes a function the compiler marked reachable.

Before v1, the project must set absolute time and peak-memory budgets on the
reference machine. Until then, the breadth series must show approximately
linear work in the number of reachable declarations. Doubling the largest
reachable set may not exceed 2.5 times declaration analysis, generic work,
optimizer input, emitted functions, time, or peak memory without documented
investigation and explicit acceptance.

Performance reports must show selected-build results separately from complete
checks. A speed claim based on silently weakening the requested validity
contract is invalid.

## Questions to Resolve

- The exact CLI names for selected and complete checking.
- Whether ordinary `test` also completes a package-wide check or only the
  selected test graph.
- The declaration-boundary representation used for lazy body loading.
- Which target and feature combinations official package publication must
  validate.
- The smallest explicit reflection and registration model compatible with
  reliable pruning.
- How separately compiled interface dispatch represents method reachability.
- Absolute performance and memory budgets on the reference machine.

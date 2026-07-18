# IDEA: Caller-Controlled Async Functions

Status: exploratory, non-normative.

SlopH should explore a Zig-inspired stackless async model in which users write
ordinary functions without declaring them `async`. The compiler infers whether
a function may suspend from its implementation and transitive calls, while each
caller explicitly chooses how the function is executed.

This separates two concerns:

- the callee determines whether suspension is possible and where it may occur;
- the caller controls waiting, concurrency, task ownership, and scheduling.

## Source Model

A function declaration does not need an `async` modifier:

```text
fn read_config(path: Path) Result[Config, Error] {
    bytes = io.read_all(path)
    return parse(bytes)
}
```

If `io.read_all` may suspend, `read_config` may suspend as well. The inferred
suspension effect is recorded in the compiled module interface so separate
compilation does not require reanalyzing dependency implementations. Changing
the implementation between suspending and non-suspending should not require
callers to rewrite the function's declaration.

The caller selects an execution mode conceptually equivalent to:

```text
// Structured call. The current task waits and may itself suspend.
config = read_config(path)

// Concurrent call. The caller receives an owned task handle.
task = spawn read_config(path)
config = join(task)

// Assert that the call completes without suspending.
config = nosuspend read_config(path)

// Drive a possibly suspending call from a synchronous boundary.
config = block_on read_config(path)
```

Names and exact syntax are deferred. The semantic distinction is required even
if standard syntax provides different spellings.

An ordinary call preserves structured control flow: if the callee suspends, the
current task suspends until the result is available. `spawn` is explicit because
it introduces concurrent lifetime and scheduling. A spawned task is an affine
owned resource and must be joined, cancelled, or explicitly detached according
to defined rules.

## Explicit Low-Level Control

The runtime should permit a low-level caller or library executor to own and
drive a generated function frame:

```text
frame = prepare function(arguments)
status = resume(frame)

match status {
    Suspended(reason) => register(frame, reason)
    Complete(value) => consume(value)
}
```

This supports application-specific executors without making a particular
scheduler part of the language semantics. The safe high-level operations can
be standard-library abstractions over this lower interface.

## Core Lowering

Async does not initially require dedicated Core expression forms. Before or
during elaboration, a potentially suspending function can become:

- an owned frame struct containing its state tag, live locals, result storage,
  and continuation information;
- a resume function that uses `Case` on the state tag;
- ordinary construction, calls, mutation, and cataloged runtime primitives.

Conceptually:

```text
potentially suspending function
        |
        v
owned Frame + resume(Frame)
        |
        v
Case on frame state + ordinary Core operations
```

The current proposed `Con`, `Case`, `Let`, `App`, and function forms are
structurally sufficient for the generated state machine. The final Core
semantics and primitive catalog must still define frame allocation, stable
addresses where required, mutation, indirect continuation calls, and the
effects of suspension and resumption.

The scheduler should normally remain a library component. Platform integration
requires a small catalog of primitives for I/O readiness, timers, wakeups, and
possibly atomics. A first implementation should prefer a single-threaded
executor so multithreaded scheduling and synchronization can be evaluated
separately.

## Ownership Rules

Only owned values may be retained in a suspended frame. Under the proposed
non-storable, call-scoped borrowing model, a temporary borrow cannot cross a
suspension point, be captured by a task, or be sent to another task.

The compiler must reject code when a potentially suspending call would keep a
temporary borrow live across suspension. This keeps checking local and avoids
introducing general lifetime parameters merely for async code.

A live frame owns all values stored within it. Its destruction semantics must
be explicit:

- successful completion transfers or destroys the result as specified;
- cancellation deterministically destroys owned locals and resources;
- dropping an incomplete frame without defined cancellation is invalid;
- resuming, joining, or consuming a frame more than permitted is invalid.

## Function Values and Effects

Although suspension annotations are normally inferred rather than written,
Core and compiled interfaces must retain the distinction. Indirect function
values must either include a `may_suspend` effect in their type or be treated
conservatively as potentially suspending.

Inference must be bounded and deterministic. Recursive call groups can compute
their suspension property as a finite fixed point over a Boolean effect. An
unknown external or indirect call is suspending unless its interface proves
otherwise.

## Boundaries and Non-Goals

This model does not make blocking operations automatically asynchronous. The
standard library and platform layer must provide suspension-aware I/O. Foreign
functions and blocking system calls remain blocking unless explicitly adapted.

The model also does not make concurrency invisible. Ordinary calls may
transparently propagate suspension, but creating an independently scheduled
task remains explicit at the call site. This preserves local readability,
structured resource ownership, and caller control.

## Questions to Resolve

- Exact source operations and names for spawn, join, cancellation, detachment,
  synchronous assertions, and synchronous entry points.
- Whether ordinary top-level entry functions implicitly receive an executor or
  must select one explicitly.
- Frame placement, pinning, layout visibility, and ABI stability.
- Cancellation timing and cleanup guarantees.
- Whether a task may migrate between threads and which memory model that needs.
- How effect-polymorphic higher-order functions represent suspension.
- Which async details remain observable across native and future managed
  execution targets.
- The minimal primitive catalog needed by a single-threaded first executor.


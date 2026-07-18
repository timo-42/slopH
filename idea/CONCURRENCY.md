# IDEA: Tasks, Threads, and Processes

Status: exploratory, non-normative, TBD.

This document surveys concurrency models in other languages and records a
possible direction for SlopH. No concurrency model is selected yet.

The first important distinction is:

```text
Task        unit of concurrent work
Thread      execution resource sharing a process address space
OS process  isolated address space and operating-system resource boundary
Actor       isolated state plus message processing inside a runtime
```

An Erlang-style process is an actor-like lightweight runtime entity, not an OS
process. SlopH terminology must not use one word for both concepts without an
explicit qualifier.

## Existing Language Philosophies

### C and C++

C and C++ expose native threads, shared memory, mutexes, condition variables,
atomics, and thread-local storage. The operating system normally performs
scheduling. Processes and their IPC mechanisms are separate platform-library
facilities.

The philosophy is maximum control with little required runtime policy.
Synchronization and lifetime correctness remain largely the programmer's
responsibility. This permits low overhead but makes data races, deadlocks,
use-after-free across threads, and unclear thread lifetime easy to express.

### Zig

Zig exposes OS threads, thread-local storage, and atomic operations directly.
Its recent standard-library direction also passes an `Io` implementation into
operations that may block or schedule work. The same synchronization API can
therefore block a native thread under a threaded implementation or yield under
an evented implementation.

Zig 0.16 replaced its earlier thread-pool direction with `std.Io` groups and
futures for much application work. It still permits explicit native threads
when the caller needs them.

The philosophy is:

> The function describes work; the caller supplies execution policy.

Zig does not attempt Rust-style static prevention of all data races. It favors
explicit control, library mechanisms, and removable overhead for
single-threaded programs.

### Rust

Rust standard threads are native threads. Ownership controls which values may
cross the thread boundary:

- `Send` permits moving a value to another thread;
- `Sync` permits sharing references to a value across threads.

Channels, mutexes, atomics, scoped threads, and async runtimes build on those
rules. Rust permits both message passing and shared-memory concurrency.

The philosophy is shared-memory systems programming with compiler-checked
ownership and data-race constraints. The tradeoff is a large conceptual
surface involving lifetimes, `Send`, `Sync`, pinning, guard types, and separate
async runtimes.

### Go

Goroutines are lightweight function executions multiplexed over OS threads.
Channels combine typed communication and synchronization. Go encourages
programs to communicate rather than share memory, while still providing
mutexes and atomics when they are the clearer mechanism.

The philosophy is to make concurrency cheap and routine. The runtime scheduler
is mandatory, shared memory and data races remain possible, and unstructured
goroutines can outlive their purpose or remain blocked indefinitely. Programs
must still make goroutine lifetime and cancellation clear.

### Erlang

Erlang processes have isolated state and communicate through mailboxes.
Messages are normally copied between process heaps. Lightweight processes,
links, monitors, and supervision trees make failure detection and worker
restart central parts of application structure.

The philosophy is:

> Isolate state, communicate with messages, and design for recovery.

This works especially well for fault-tolerant services and distribution. It
depends on a substantial managed runtime, garbage collection, mailbox
semantics, message copying, and a failure model different from native systems
programming.

External native programs remain real OS processes and use separate Erlang
interoperability mechanisms.

### Java Virtual Threads

Java provides both platform threads backed by OS threads and lightweight
virtual threads scheduled by the runtime. Virtual threads preserve ordinary
synchronous, blocking code while allowing many tasks to share fewer carrier
threads.

The philosophy is to keep the thread-per-request programming model while
making threads cheap enough to represent application tasks. Virtual threads
improve throughput rather than individual operation latency. Native and foreign
calls can pin a virtual thread to its carrier and reduce scalability.

### Swift

Swift makes structured tasks, task groups, actors, and `Sendable` types primary.
Tasks form a hierarchy, cancellation propagates through that hierarchy, actors
serialize access to their state, and sendability restricts values crossing
concurrency domains. Application code normally does not control underlying
threads directly.

The philosophy is structured lifetime and language-enforced isolation. This
offers strong guarantees but makes actor isolation, executor selection,
sendability, and concurrency annotations a significant language subsystem.

## Design Axes

The SlopH decision should evaluate at least these independent axes.

### Unit of Application Work

Candidate primary units are:

- native threads;
- stackless async tasks;
- virtual or green threads;
- actors with mailboxes;
- explicit callbacks and event loops.

Choosing tasks as the primary unit does not prevent native threads. It means
thread count and task count are separate concepts.

### Scheduling Ownership

Scheduling can be:

- fixed by the language runtime;
- selected globally by the application;
- passed explicitly as an executor or I/O capability;
- controlled directly by callers at each spawn boundary.

The caller-controlled async idea in [ASYNC.md](./ASYNC.md) favors explicit
application or caller policy over a mandatory global executor.

### State Sharing

Concurrent domains can exchange data through:

- unrestricted shared memory;
- compiler-checked shared references;
- explicit synchronized shared cells;
- ownership-transferring channels;
- copied actor messages;
- serialization across OS processes.

The proposed unique-ownership model naturally supports moving owned values
through channels. It does not yet provide a simple foundation for arbitrary
shared mutable graphs.

### Lifetime Structure

Spawned work can be:

- unstructured and independently detached;
- owned by an affine join handle;
- scoped to a lexical block;
- owned by a task group;
- supervised and restarted after failure.

Structured ownership makes leaks and abandoned work easier to diagnose but
requires defined cancellation and cleanup rules.

### Failure Isolation

Tasks and threads share an address space. Memory corruption or an unrecoverable
runtime failure can affect the entire process. OS processes provide a stronger
failure and security boundary at higher startup, memory, and communication
cost.

Actor runtimes provide logical isolation but cannot fully contain corruption in
unsafe native code running inside the same OS process.

## Possible SlopH Direction

The leading direction, still TBD, separates the public concepts:

```text
Task      cheap work scheduled by an executor
Thread    native parallel execution sharing memory
Process   isolated operating-system execution with explicit IPC
Actor     library abstraction built from a task and typed mailbox
```

Conceptual operations could be:

```text
task = task.spawn(function, owned_arguments)
value = task.join(task)

thread = thread.spawn(function, owned_arguments)
value = thread.join(thread)

process = process.spawn(specification, capabilities)
status = process.wait(process)
```

Names and syntax are illustrative.

### Tasks

Tasks could be the default concurrency abstraction:

- a task is an owned stackless async frame;
- an executor may run tasks on one or multiple native threads;
- ordinary calls propagate suspension without spawning concurrency;
- spawning is explicit;
- task groups own child tasks;
- leaving a group joins or cooperatively cancels its children;
- detachment is explicit and changes resource ownership visibly.

This combines caller-controlled scheduling with structured lifetime. It avoids
requiring one native thread per concurrent I/O operation.

### Threads

Native threads remain an explicit standard-library resource for:

- CPU-bound parallel work;
- blocking C functions;
- thread-affine foreign APIs;
- custom executors;
- low-level systems software.

A thread handle should be affine. It must be joined or explicitly detached.
Dropping a live thread handle silently is invalid. Arbitrary forced thread
cancellation should not be offered because it cannot reliably preserve owned
resource cleanup or foreign-library invariants.

A spawned thread closure captures owned values by move. Temporary borrows do
not cross the thread boundary under the current memory proposal.

### Channels and Shared State

Ownership-transferring channels are the leading initial communication model.
Sending a non-copy value moves it into the channel and eventually to the
receiver. This permits tasks or threads to communicate without shared mutable
access to the payload.

Shared mutable memory can be introduced later through explicit types such as:

- mutex-protected cells;
- read/write locked cells;
- atomic values;
- deliberately shared immutable allocations;
- shared arenas or handles.

These abstractions require a decision about shared ownership and memory
reclamation that the current memory design has not made. They should not be
assumed merely because native threads exist.

An actor can initially be a library abstraction containing owned state, a
typed channel receiver, and a task processing messages sequentially. Actors do
not require a new Core expression form.

### OS Processes

OS processes remain visibly different from tasks and threads. A process API
must describe:

- executable and arguments;
- environment policy;
- inherited or supplied capabilities;
- standard-stream pipes or handles;
- working-directory policy;
- exit status and termination;
- signals or platform termination mechanisms;
- serialization and IPC framing.

A child-process handle should be affine and must be waited for, explicitly
detached, or terminated through defined policy. Process spawning belongs in a
later Host ABI group described in [INTEROP.md](./INTEROP.md).

Processes are appropriate for external tools, independent services, untrusted
or crash-prone work, and operations requiring a stronger isolation boundary.
They are too expensive and semantically different to be transparently
substituted for ordinary tasks.

### Foreign Calls

A foreign call can be synchronous and still block its executing native thread.
It does not become a suspension point automatically.

Foreign declarations or wrappers should expose blocking behavior. The caller
or executor may then:

- run the call directly and accept blocking;
- move it to a dedicated worker thread;
- use a nonblocking provider API integrated with the executor;
- isolate it in an OS process.

This is particularly important for crypto, database, compression, and legacy C
libraries.

## Preliminary Philosophy

A possible SlopH philosophy is:

> Tasks express concurrent work, threads provide parallel execution, processes
> provide isolation, and ownership makes every boundary explicit.

This would combine:

- Zig-like caller control and library-defined scheduling;
- Rust-like ownership transfer at concurrency boundaries;
- Go-like channels as the preferred initial communication mechanism;
- Swift-like structured task lifetime;
- Erlang-like actors and supervision as optional library patterns.

The initial language could support tasks, ownership-transferring channels, and
explicit worker threads without immediately supporting arbitrary shared mutable
state. This is only a leading hypothesis and must be tested with real servers,
parallel CPU workloads, blocking C libraries, subprocess pipelines, and clean
shutdown behavior.

## Validation Questions

Example programs should test:

- structured fan-out and result collection;
- cancellation and deterministic cleanup of task-owned resources;
- bounded producer/consumer channels and backpressure;
- CPU work distributed across native threads;
- blocking C calls isolated on workers;
- thread-affine C handles;
- task and thread panic or trap propagation;
- actor-style state ownership and mailbox shutdown;
- subprocess pipelines, exit status, and broken pipes;
- forced process termination and resource cleanup;
- single-threaded builds with concurrency overhead removed;
- deterministic tests under different valid schedules;
- behavior on native and future managed targets.

Measurements should include task and thread creation cost, idle memory, context
switching, channel throughput, cancellation latency, scheduler fairness,
parallel speedup, blocked-worker behavior, process startup, and complete build
latency added by concurrency analysis.

## TBD

- Whether tasks, native threads, or another abstraction are primary in v1.
- Whether an executor is passed explicitly, selected at program startup, or
  provided by a standard default.
- Whether the first executor is single-threaded, multithreaded, or both.
- Exact task, thread, process, group, join, detach, and cancellation APIs.
- Whether detached tasks and threads are permitted in v1.
- Cancellation semantics and cleanup guarantees.
- Channel ownership, buffering, closing, selection, and fairness rules.
- Whether channel endpoints can cross OS process boundaries.
- Whether shared immutable values or synchronized shared cells exist in v1.
- The memory model and atomic-ordering catalog.
- Whether lexical scoped threads are worth extending the borrow model.
- Whether a `Send`-like property is explicit, inferred, or unnecessary under
  ownership transfer.
- How traps, panics, and ordinary errors propagate through joins.
- Actor addressing, supervision, restart, and mailbox policy.
- Process spawning, IPC, signal, environment, and capability APIs.
- How blocking foreign calls declare their scheduling behavior.
- Thread-local and task-local storage.
- Scheduler observability, deterministic testing, and debugging facilities.
- Relationship between native executors and future managed targets.

## References

- [Zig 0.16 I/O, synchronization, and task direction][zig-io]
- [Zig language thread and atomic facilities][zig-language]
- [Rust concurrency overview][rust-concurrency]
- [Go concurrency philosophy][go-concurrency]
- [Go guidance on goroutine lifetime][go-lifetime]
- [Erlang lightweight processes][erlang-processes]
- [Erlang supervision trees][erlang-supervision]
- [Java virtual threads][java-virtual]
- [Swift structured concurrency, actors, and Sendable][swift-concurrency]

[zig-io]: https://ziglang.org/download/0.16.0/release-notes.html
[zig-language]: https://ziglang.org/documentation/0.16.0/
[rust-concurrency]: https://doc.rust-lang.org/book/ch16-00-concurrency.html
[go-concurrency]: https://go.dev/doc/effective_go#concurrency
[go-lifetime]: https://go.dev/wiki/CodeReviewComments#goroutine-lifetimes
[erlang-processes]: https://www.erlang.org/doc/system/eff_guide_processes.html
[erlang-supervision]: https://www.erlang.org/doc/system/design_principles.html
[java-virtual]: https://docs.oracle.com/en/java/javase/26/core/virtual-threads.html
[swift-concurrency]: https://docs.swift.org/swift-book/LanguageGuide/Concurrency.html

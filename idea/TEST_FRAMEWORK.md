# IDEA: Bundled Testing, Interceptors, and Fault Injection

Status: exploratory, non-normative.

SlopH should ship an official test framework through the compiler's bundled
package set and integrate discovery and execution into the single CLI. A fresh
offline toolchain should support unit, integration, parameterized, property,
stateful, documentation, and failure-path testing without first finding a
third-party runner.

The framework should favor typed fixtures, explicit capabilities, protocol
interceptors, deterministic simulators, and real boundary fault injection. It
should not center testing on expectation-heavy mock objects or arbitrary global
monkeypatching.

In particular, the framework must make it straightforward to test:

- failure to open, read, write, sync, rename, or close a file;
- permission denial, missing paths, full storage, exhausted descriptors, short
  reads and writes, interruption, delay, and cancellation;
- allocation failure;
- network errors, malformed responses, disconnects, and timeouts;
- an arbitrary native syscall returning a selected error;
- cleanup or recovery failing after an earlier failure;
- crashes and durability invariants where simulation can state its assumptions.

## Package and Compiler Boundary

The test APIs are ordinary official packages, conceptually divided into:

```text
test.core       assertions, results, fixtures, parameters, capture, replay
test.property   generators, shrinking, state machines, regression corpus
test.fault      semantic fault plans and systematic exploration
test.platform   target-specific subprocess and syscall injection
```

Names and the final split are deferred. The compiler recognizes test metadata
and records test and fixture entries in compiled interfaces so the CLI can
discover tests without executing package code. Test declarations compile
through the ordinary language and Core; production programs do not link test
packages unless explicitly requested.

The framework belongs in the compiler-shipped batteries bundle, not the
mandatory runtime `core`. Independently versioned libraries may ship additional
test-support packages using the same fixture, interceptor, and result protocols.

## Lessons from Popular Frameworks

### pytest: hierarchical fixtures

pytest's `conftest.py` makes fixtures visible to tests in a directory and its
descendants without imports. Nested directories can add or override fixtures;
fixtures can depend on fixtures, have bounded lifetimes, use `yield` teardown,
and be parameterized. pytest also supplies a unique temporary directory per
test. See its [fixture reference](https://docs.pytest.org/en/stable/reference/fixtures.html),
[fixture API](https://docs.pytest.org/en/stable/reference/reference.html#pytest.fixture),
[parametrization guide](https://docs.pytest.org/en/stable/how-to/parametrize.html),
and [`tmp_path` guide](https://docs.pytest.org/en/stable/how-to/tmp_path.html).

SlopH should adopt directory-scoped fixture composition and deterministic
teardown, while making every resolved fixture and hook visible in inspection
output. It should not copy unconstrained monkeypatching. pytest itself warns
that patching builtins can break its runner and recommends explicit dependency
passing for controlled code; see the
[monkeypatch guide](https://docs.pytest.org/en/stable/how-to/monkeypatch.html).

### requests-mock: intercept at the transport boundary

`requests-mock` works because Requests funnels transport through an adapter. A
custom adapter matches requests and returns registered responses; a pytest
fixture installs the adapter, and the mocker can optionally pass unmatched
requests to real HTTP. It also provides request matching, response sequences,
exceptions, and request history. See the
[`requests-mock` project](https://pypi.org/project/requests-mock/),
[documentation](https://requests-mock.readthedocs.io/en/latest/),
[pytest fixture](https://requests-mock.readthedocs.io/en/latest/pytest.html), and
[mocker activation](https://requests-mock.readthedocs.io/en/latest/mocker.html).

This is a good form of test double: it intercepts one stable protocol boundary
rather than replacing arbitrary implementation functions. SlopH should support
the same model through typed transport capabilities and scoped interceptor
fixtures.

### Go: small built-in test operations

Go demonstrates the value of ordinary test functions, named subtests, selective
execution, controlled parallelism, cleanup callbacks, temporary directories,
environment restoration, helpers, table-driven cases, benchmarks, and fuzzing
inside the standard toolchain. Its fuzzer minimizes failures, persists them in
a corpus, and prints an exact replay command. See the official
[`testing` package](https://pkg.go.dev/testing),
[table-driven test guidance](https://go.dev/wiki/TableDrivenTests), and
[fuzzing documentation](https://go.dev/doc/security/fuzz/).

SlopH should retain this small ordinary-function feel and make every parameter
or subtest an independently selectable result.

### JUnit, Rust, ExUnit, and Common Test

JUnit contributes parameterized and nested tests, tags, timeouts, temporary
directory injection, and one coherent extension model. Rust distinguishes unit,
integration, and documentation tests and permits typed `Result` failures.
ExUnit provides opt-in parallel suites, tags, setup contexts, and log capture.
Erlang Common Test supplies shared suite/group/case lifecycle hooks for external
systems. See the [JUnit guide](https://docs.junit.org/current/user-guide/),
[Rust testing chapter](https://doc.rust-lang.org/stable/book/ch11-01-writing-tests.html),
[ExUnit.Case](https://hexdocs.pm/ex_unit/ExUnit.Case.html), and
[Common Test hooks](https://www.erlang.org/doc/apps/common_test/ct_hooks_chapter.html).

SlopH should use one typed fixture/hook mechanism, structured test trees,
explicit parallel-safety rules, and scoped output capture. It should avoid
runtime reflection, automatic service-loader plugins, and annotation
proliferation.

### QuickCheck, Hypothesis, and Proptest

Property frameworks generate cases and shrink failures toward small
counterexamples. Hypothesis state machines generate action sequences and print
a short reproducible program. Proptest persists failures for replay before new
generated inputs. See [QuickCheck shrinking](https://hackage.haskell.org/package/QuickCheck/docs/Test-QuickCheck.html),
[Hypothesis stateful testing](https://hypothesis.readthedocs.io/en/latest/stateful.html),
[Proptest](https://proptest-rs.github.io/proptest/), and
[failure persistence](https://proptest-rs.github.io/proptest/proptest/failure-persistence.html).

SlopH should ship deterministic value strategies, shrinking, state-machine
actions and invariants, persistent regression artifacts, and exact seed replay.

### SQLite and FoundationDB: systematic failures

SQLite simulates allocation and I/O failures and repeats an operation while
advancing the failure occurrence. It tests both one-shot and persistent failure,
compound faults, crashes, and post-failure integrity. FoundationDB runs seeded
deterministic simulations of networks, disks, time, and machine failures; its
`Buggify` mechanism injects errors unlikely to occur in development. See
[How SQLite Is Tested](https://www.sqlite.org/testing.html),
[SQLite VFS shims](https://www.sqlite.org/vfs.html),
[FoundationDB simulation](https://apple.github.io/foundationdb/testing.html),
and [client testing](https://apple.github.io/foundationdb/client-testing.html).

Occurrence sweeping, one-shot and sticky faults, deterministic schedules,
compound faults, and post-failure invariants should be standard SlopH testing
concepts.

### Native syscall injection

Linux `strace` can inject symbolic errors, return values, signals, and delays at
selected syscall occurrences and can combine injection with path filters. See
the [`strace` manual](https://www.man7.org/linux/man-pages/man1/strace.1.html).

SlopH should expose equivalent intent through an optional target-specific
subprocess runner. This complements portable semantic interception; it does not
replace it.

## Basic Test Model

Tests are ordinary typed functions carrying compiler-recognized test metadata:

```text
test "empty input is rejected" {
    assert_equal(parse(""), Err(ParseError.Empty))
}
```

A test may return `Unit`, `Result[Unit, E]`, or another explicitly admitted
test-result type. An `Err` becomes a structured failure with source provenance.
Traps fail unless the test expects an exact stable trap class and message
identity.

The initial assertion surface should include:

- truth, falsehood, equality, inequality, and structural diff;
- ordered and explicitly policy-controlled approximate numeric comparison;
- `Option` and `Result` constructors;
- expected errors, traps, cancellation, timeout, and effects;
- canonical golden or snapshot values.

Assertions evaluate operands once and report source expressions, typed values,
fixture and parameter context, seed, fault schedule, and replay command.

Test identities form a hierarchy:

```text
package :: module :: group :: test :: parameter :: subtest
```

Filtering uses resolved identities, tags, source paths, and explicit selection
expressions. Human output is concise; JSONL is the canonical machine stream.

## pytest-Like Directory Fixtures

A reserved test-support module, conceptually `test.scope.sloph`, supplies
fixtures to tests in its directory and descendants. Nested scope modules add
fixtures and may explicitly extend or replace an ancestor fixture.

Lookup starts at the test and walks outward through its group, local scope,
ancestor scopes, explicitly imported fixture packages, and built-ins. Same-
level ambiguity is an error. Ancestor replacement must be explicit. Inspection
shows the selected fixture identity and complete lookup path.

Conceptually:

```text
fixture workspace(context: TestContext) Workspace {
    directory = context.temp_directory()
    yield Workspace(directory)
    verify_clean(directory)
}

test "writes output" uses workspace {
    ...
}
```

The typed fixture graph is acyclic and discoverable without execution.
Fixtures request dependencies explicitly. Initial lifetimes are `test`,
`group`, `package`, and `session`, with `test` as the default. Wider lifetimes
must satisfy ownership and concurrency requirements.

Owned fixtures are destroyed in reverse dependency order. Cleanup callbacks
cover external state. All safe cleanups run after failure or cancellation, and
cleanup errors are reported alongside the primary failure.

There is no implicit autouse fixture by default. Explicit directory hooks may
wrap tests, but appear as graph edges and execute in stable order. This retains
`conftest` convenience without hiding behavior from AI review.

Built-ins include isolated temporary directories, deterministic clock and
randomness, captured output and logs, test-scoped environment and arguments,
loopback networking, subprocess management, leak checks, interceptor routers,
and fault control.

## Who Ships Interceptors?

The shared framework supplies generic infrastructure:

- typed scoped installation and teardown;
- deterministic request routing and ordered response sequences;
- request recording and structured history;
- strict unmatched-request failure and explicit pass-through policy;
- delay, cancellation, disconnect, malformed-data, and injected-error support;
- replay, diagnostics, resource bounds, and concurrency isolation.

The library that owns a protocol supplies protocol knowledge in a separate test
support package:

- the stable transport or capability interface;
- request normalization and matching semantics;
- response and error builders;
- a conformance suite usable by real and intercepted implementations;
- optional protocol simulator and common fixtures.

For example, the official HTTP library should ship `http.test`, built on the
generic interceptor router. It understands methods, normalized URLs, paths,
query parameters, headers, bodies, streaming, status codes, connection failure,
and response sequencing. The general test framework should not hard-code HTTP
semantics.

An application library normally does not build its own interception engine. It
accepts an `HttpClient`, `FileSystem`, `Clock`, or other capability and uses the
test support provided by the capability owner. A domain protocol library may
ship higher-level request builders, example responses, and a conformance suite
when its semantics add value.

Test-support packages are development dependencies and do not enter production
artifacts. Published protocol libraries should run their conformance suite
against both the real implementation and their simulator or interceptor.

## Typed Call Router

The reusable abstraction is a typed, scoped call router for explicitly
interceptable function boundaries. It is not a reflective mechanism that can
replace any function by source-name string.

Each interceptable declaration exposes a compiler-validated call schema
conceptually equivalent to:

```text
Call[
    FunctionIdentity,
    Arguments,
    ReturnType,
    ErrorType,
    Effects,
]
```

The function identity is a resolved declaration identity. Arguments, returned
values, errors, ownership transfers, and effects remain statically typed. A
router created for `HttpTransport.send` cannot accidentally intercept
`FileSystem.open` or return a value with the wrong type.

Conceptually:

```text
router = intercept(HttpTransport.send)

router
    .when(method: GET, url: "https://config.test/v1")
    .return(HttpResponse(status: 200, body: config))

result = load_config(router.transport())
```

Exact syntax is deferred. Match rules operate on stable typed argument
projections. A rule may:

```text
return(value)
error(error)
trap(trap)
delay(duration).then(outcome)
suspend_until(event)
cancel()
short_return(value)
delegate()
answer(responder)
```

`delegate` calls the wrapped real implementation and records its outcome. It
is explicit and may be restricted by a host, path, capability, or operation
allowlist. A dynamic responder receives typed arguments and a bounded
`CallContext` and returns an admitted outcome. Responders are deterministic by
default and cannot silently acquire capabilities absent from their fixture.

Rules have stable declaration order. If several rules match one call, the
protocol-specific matcher either applies one documented specificity rule or
reports ambiguity; it does not depend on hash-map or plugin order. Response
sequences consume outcomes deterministically and report exhaustion.

## Call and Return History

Every intercepted or delegated invocation produces a canonical record:

```text
CallRecord {
    sequence
    function_identity
    arguments
    matched_rule
    outcome
    origin
    task_identity
    logical_time
    source_provenance
}
```

`origin` distinguishes injected, delegated, and real outcomes. `outcome` is a
closed typed family conceptually containing:

```text
Returned(value)
Failed(error)
Trapped(trap)
Cancelled
TimedOut
```

History records calls and successful or unsuccessful returns, not only the
requests that matched expectations. Recording is bounded by count and byte
limits; exceeding a limit is a test failure rather than silent truncation
unless the test explicitly selects a documented sampling policy.

Tests normally assert on history after exercising the subject:

```text
history = router.history()

assert_equal(history.count(), 2)
assert_equal(history[0].arguments.method, GET)
assert_equal(history[0].outcome, Returned(expected_response))
assert(history.none { call -> call.arguments.url.host != "config.test" })
```

The framework may also compare an exact canonical transcript:

```text
assert_calls(router, [
    Call(HttpTransport.send, request, Returned(response)),
    Call(Cache.store, entry, Returned(Unit)),
])
```

Exact transcripts are useful for narrow protocol tests and reviewed snapshots.
Application tests should usually assert only externally meaningful calls and
outcomes so harmless implementation changes do not break them.

## Routing versus Expectations

The design separates response routing from verification:

- routing rules determine how an intercepted call behaves;
- history assertions verify what actually occurred;
- optional expectations provide concise count or absence constraints.

This avoids requiring every permitted call and total order to be declared
before the test executes. Optional expectations remain useful:

```text
router.expect(GET, url).exactly(1)
router.expect_no_unmatched_calls()
router.expect_no_real_calls()
```

Expectations are verified during fixture teardown. When the test already has a
primary failure, unmet expectations and cleanup problems are reported as
secondary failures and cannot replace the original diagnostic.

Strict mode is the default for external transports: an unmatched call fails
immediately with the closest rules and a structured mismatch. A recorder-only
mode delegates and records without predeclared responses. Pass-through mode is
explicit, allowlisted, and marks the result as an integration test.

## Concurrency and Ordering

A global total order across concurrently running tasks is often accidental and
nondeterministic. Each record therefore carries per-router sequence,
task identity, logical time, parent call, and protocol correlation identifiers
where available.

The assertion library supports per-task order, partial order, and correlation:

```text
assert_before(history, connect_call, send_call)
assert_same_task(history, first_read, retry_read)
assert_correlated(history, request_call, response_call)
```

An exact global transcript is valid only under a deterministic scheduler or an
explicit serial fixture. Parallel execution must not change matching results,
per-task order, or canonical grouping merely because host scheduling changed.

## Interceptable Boundary Rules

V1 supports declarations designed to expose an interception boundary:

- capability and transport operations;
- protocol client boundaries;
- runtime platform operations;
- generated FFI calls;
- registered callbacks;
- syscall wrappers.

Ordinary local functions are not transparently interceptable by default.
Universal function replacement would interfere with inlining, specialization,
reachability, ownership, direct versus indirect calls, tail calls, native ABI
behavior, and local reasoning.

A future explicit test instrumentation option may rewrite calls to selected
resolved declarations:

```text
--intercept package::module::function
```

Such instrumentation is test-only, included in artifact identity, visible in
the selected graph, and never enabled implicitly by installing a plugin.

Raw pointers and borrowed foreign memory cannot be recorded generically.
Generated FFI bindings describe safe argument projections, for example copying
exactly the `length` bytes passed to `write`. Protocol packages may add bounded
decoders and redaction rules. Secrets, large buffers, handles, and opaque
pointers are not serialized without an explicit safe policy.

## Proper Interceptor Use

Interception should occur at the narrowest stable boundary that preserves the
behavior the test intends to check:

```text
application
    -> domain client
        -> HttpClient capability
            -> interceptor or real transport
```

The preferred form is explicit scoped injection into an owned client:

```text
test "loads remote configuration" uses http_interceptor {
    http_interceptor.when(GET, "https://config.test/v1")
        .respond(status: 200, body: valid_config)

    client = ConfigClient(http_interceptor.client())
    assert_equal(client.load(), Ok(expected))
    assert_equal(http_interceptor.history().count(), 1)
}
```

The exact syntax is deferred. Matching may inspect method, normalized URI,
selected headers, and typed or byte body. Unmentioned fields are not silently
asserted. History assertions should usually verify externally meaningful facts,
not reproduce every internal call.

Unmatched requests fail by default with the closest registered routes and a
structured mismatch explanation. Real network pass-through requires an
explicit allowlist, is recorded in results, and makes the test an integration
test. Tests cannot accidentally contact the public network.

Interceptors are scoped values, not global patches. Parallel tests receive
independent routers and histories. A default client supplied by program startup
may be replaced through the test's capability environment, but arbitrary
module functions cannot be patched.

Use interception to test application request construction, response handling,
retry, timeout, and error logic. Also retain:

- pure tests for parsing and domain decisions;
- protocol conformance tests for the interceptor itself;
- loopback integration tests for serialization and real sockets;
- target-specific syscall tests for the native runtime boundary;
- selected live-system tests when an external service contract requires them.

An interceptor is not proof that a real server behaves like the registered
responses. Its value is deterministic control of the client-side boundary.

## Prefer Models over Mock Objects

V1 should not ship a general expectation-based mock-object DSL. Such mocks
often couple tests to call order and private structure and can let an incorrect
model confirm an incorrect implementation.

Preferred tools are:

- real values and pure functions;
- typed capabilities supplied through fixtures;
- in-memory filesystem, clock, queue, and transport models with specified
  semantics;
- protocol interceptors like `requests-mock` at stable boundaries;
- event recorders for after-the-fact assertions;
- deterministic simulators;
- real subprocess and integration tests where the boundary matters.

A test double is an ordinary typed implementation. It does not bypass
visibility, replace globals, or dispatch by method-name strings.

## Portable Fault Controller

Every fallible standard capability operation and trusted runtime boundary has
a stable semantic operation identity, for example:

```text
fs.open          fs.read          fs.write        fs.sync
fs.rename        net.connect      clock.now       random.read
process.spawn    memory.allocate  task.wait       task.wake
```

An owned test fixture configures a `FaultPlan`:

```text
faults.fail_next(fs.open, FileError.PermissionDenied)
faults.fail_nth(fs.write, 3, FileError.NoSpace)
faults.fail_after(fs.read, 5, sticky, FileError.IO)
faults.short_nth(fs.write, 2, bytes: 7)
faults.delay_next(net.connect, 10.seconds)
faults.cancel_next(task.wait)
```

Rules may filter stable semantic arguments such as a capability-relative path.
They may not depend on pointers, unrelated scheduling, or unstable internal
calls. A fired rule records operation identity, occurrence, result, call
provenance, and remaining plan.

Supported behaviors include one-shot and sticky failure, short successful
operations, interruption and retry, delay, cancellation, allocation exhaustion,
disconnect, malformed input, and bounded compound plans.

Production capabilities remain direct. Test builds wrap selected capabilities
with the controller. Explicit authority prevents application code from
bypassing injection through ambient APIs.

## Systematic Failure Exploration

The framework automates SQLite-style occurrence sweeping. A baseline run records
fallible operations. Subsequent runs inject at occurrence 1, then 2, until the
operation completes without discovering another point. The test can select
one-shot, sticky, operation filters, or bounded compound depth.

```text
failure_test "save is atomic" explore each_failure_once {
    run save(document)
    invariant old_or_new_file_is_valid()
    invariant no_resources_leaked()
}
```

After each run, invariants execute with injection disabled unless the test is
explicitly exploring recovery failures. Runs, operations, depth, time, memory,
and output are bounded. Reports include the minimal known schedule and every
unhandled error, leak, or invariant failure.

Schedules identify semantic operations, not only raw call numbers, so a source
change cannot silently redirect an old regression schedule to an unrelated
operation.

## Real Syscall Failure Tests

Capability injection tests application behavior but does not prove that the
native runtime translates actual operating-system failures correctly. The
runtime should funnel its syscalls through a small documented platform layer
that test builds can interpose.

On Linux, an isolated child runner may additionally use a compatible
`ptrace`/`strace --inject` backend:

```text
<lang> test native-open-failure \
    --inject-syscall openat:error=EACCES:when=1

<lang> test interrupted-write \
    --inject-syscall write:error=EINTR:when=3
```

The runner records OS, architecture, syscall, filters, occurrence, injected
result, child trace, and exit status. Unsupported target injection is reported
as unsupported, never passing.

Opaque foreign libraries may bypass SlopH's platform layer. A tracer can cover
some cases, but portable testing needs the foreign library's own fault hooks or
a controlled subprocess. Results must state the boundary actually exercised.

Semantic fault tests are the normal fast suite. Native syscall tests validate
runtime translation and integration. Both are valuable and neither is a
general mock.

## Parameters, Properties, Fuzzing, and Snapshots

Table cases are ordinary data and each case is independently reported. Property
tests take explicit strategies, seeds, budgets, and shrink policy. Failures are
minimized, printed canonically, saved as regression artifacts, and replayed
before new cases.

State-machine tests declare actions, preconditions, model state, and invariants.
They shrink both inputs and action sequences and print a copyable reproduction.
Coverage-guided fuzzing is a slower mode over the same corpus; ordinary tests
replay committed failures without starting an unbounded session.

Canonical snapshots are committed review artifacts. Updating them is an
explicit command producing normal diffs. Bulk acceptance cannot silently hide
semantic changes.

## Isolation and Parallelism

Tests run in parallel only when effects and fixtures prove isolation. Tests
using process-global environment, working directory, signals, raw syscall
tracing, fixed ports, or shared external services use an exclusive group or
separate process.

Every test receives explicit time, randomness, filesystem, network,
environment, and subprocess capabilities. Seeds derive from test identity and
invocation seed, not execution order. Output is buffered per test and reported
in stable identity order.

Timeouts use simulated deadlines where applicable and a real runner watchdog.
The report distinguishes simulated time, CPU time, blocked external work, and
forced termination. Teardown checks owned resources, tasks, descriptors,
temporary files, and callbacks.

## Discovery, Reachability, and Results

Discovery reads interface metadata without running tests or fixtures. Only
selected tests, fixture graphs, parameter cases, generators, interceptors,
application declarations, and runtime support become compilation roots.
Unselected tests cause no specialization, optimization, or code generation.

Complete test-package validation remains required in CI and before publication.
Changing one test fixture invalidates only consumers of that fixture. Graph
inspection explains discovery, fixture selection, reachability, and
invalidation.

Every structured result includes identity, status, resource measurements,
assertion diff, fixtures and cleanup, captured output, seed and minimized input,
fault schedule, interceptor history, compiler and target versions, and an exact
replay command. Optional JUnit XML is an export; JSONL carries the complete
schema.

## Performance and Conformance Tests

The framework corpus includes at least:

- 100,000 discoverable tests across 150 packages;
- fixture trees of depths 1, 8, and 32 and fanout to 1,000 tests;
- 10,000 parameter cases with only one selected;
- property and state-machine failures requiring shrinking;
- interceptors with 1, 10, 100, and 1,000 routes and history entries;
- failure sweeps over 1, 10, 100, and 1,000 operations;
- one-shot, sticky, short, delay, cancellation, allocation, compound, crash,
  and native syscall cases;
- four concurrent isolated worktree runs.

Measure discovery, graph building, checking, compilation, linking, startup,
fixture setup, execution, teardown, route matching, shrinking, fault
exploration, replay, and serialization separately. Record wall time, CPU time,
peak memory, files read, interfaces and bodies loaded, declarations compiled,
processes started, exploration runs, and output bytes.

Structural tests prove that:

- discovery executes no package or fixture code;
- fixture lookup, interceptor matching, teardown, and output order are
  deterministic;
- selected runs compile no unrelated tests or fixtures;
- unmatched network requests fail and never escape without an allowlist;
- cleanup runs after assertion, returned error, timeout, and injected failure
  where safe;
- minimized failures and fault schedules replay exactly;
- one-shot and sticky sweeps visit their documented occurrences;
- syscall injection affects only its child and reports unsupported targets;
- complete checking finds invalid unselected tests;
- deleting caches changes performance only.

Before v1, absolute discovery and feedback budgets must be set on the reference
machine. A framework feature may be rejected when its discovery, fixture,
plugin, interceptor, or reporting cost dominates small tests.

## Initial Delivery

The first official version should include:

1. unit and integration tests with typed results and structural assertions;
2. hierarchical fixtures with deterministic cleanup and temporary resources;
3. table cases, selection, tags, output capture, replay, and JSONL results;
4. an interceptor router plus an official HTTP test-support package;
5. semantic filesystem fault injection with next, nth, sticky, short-write, and
   occurrence-sweep modes;
6. selected-test compilation and parallel isolated execution.

Property/state-machine generation, coverage fuzzing, crash simulation, and raw
syscall injection may arrive incrementally. Their future requirements must not
be blocked by the initial fixture, capability, interceptor, result, or replay
formats. A generic mock-object framework is not part of v1.

## Questions to Resolve

- Final test, fixture, parameter, and hook syntax.
- The reserved directory fixture filename and module-system interaction.
- Whether ancestor fixtures may be replaced or only extended under new names.
- Exact package boundaries for core tests, properties, faults, HTTP support,
  and platform injectors.
- Which semantic operations receive stable v1 fault identities.
- Argument filtering without leaking secrets or unstable representations.
- Interceptor matching, normalization, streaming, and pass-through semantics.
- Syscall injection strategy on macOS and Windows.
- Honest named storage profiles for crash and power-loss simulation.
- Default exploration, shrinking, timeout, memory, process, and output limits.

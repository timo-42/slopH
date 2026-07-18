# IDEA: Official Observability Packages and Virtual Bundle

Status: exploratory, non-normative.

SlopH should provide independently versioned official packages for traces,
metrics, structured logs, OpenTelemetry, OTLP, Prometheus, instrumentation
transformations, and observability testing. A virtual `observability` package
should select a tested compatible version set, analogous to the virtual bundle
for the extended standard library.

These are extended libraries, not language or Core features. Applications may
depend on the virtual bundle for convenience or select individual packages for
a smaller graph or alternative providers. OpenTelemetry is the canonical
correlation and semantic model. Prometheus is an exporter from the shared
metrics model, not a second instrumentation API.

## Goals

- Give libraries small stable APIs for traces, metrics, and structured logs.
- Keep SDKs, exporters, protocols, and vendors out of library dependencies.
- Support OpenTelemetry conventions, W3C propagation, OTLP, Prometheus,
  OpenMetrics, structured JSONL, and human console output.
- Correlate signals across calls, tasks, async suspension, and transports.
- Instrument stable official APIs and small semantic interfaces through
  ordinary library-provided transformations.
- Let rich implementations expose only the stable facts telemetry needs.
- Bound CPU, memory, queues, cardinality, output, and failure behavior.
- Keep disabled instrumentation cheap and unused packages unreachable.
- Provide deterministic recording and assertions for tests.

## Non-Goals

- New syntax or Core forms for telemetry.
- A mandatory telemetry runtime, exporter, or background thread.
- One build unit that forces every observability implementation into a program.
- A separate Prometheus metrics API.
- Treating APM as a fourth signal beyond traces, metrics, and logs.
- Tracing every function, allocation, platform operation, or syscall.
- Inferring safe exported values from arbitrary argument names.
- Standardizing timeouts, pools, TLS, or other implementation details merely
  because a concrete HTTP or database library supports them.
- Allowing production observers to change calls or outcomes.

## Concrete Packages

The provisional package set is:

```text
observability-api
    context, attributes, resources, scopes, no-op providers

observability-trace
    tracers, spans, sampling contracts, span processors

observability-metrics
    meters, instruments, aggregation contracts, readers

observability-log
    structured records, events, console and JSONL sinks

observability-transform
    transformations and semantic instrumentation adapters

observability-otel
    OpenTelemetry mapping, semantic conventions, propagation

observability-otlp
    OTLP encoders and exporters over official transports

observability-prometheus
    Prometheus/OpenMetrics conversion and scrape handler

observability-testing
    deterministic clock, in-memory provider, assertions, fault fixtures

observability-profile
    later profiling signal, symbolization, profile interchange, flame graphs

observability
    virtual package selecting a tested compatible version set
```

Names are provisional, but dependency direction is not. Signal packages depend
only on the small shared API. Protocol and backend packages depend on signal
APIs, never the reverse. Instrumented libraries do not depend on OTLP,
Prometheus, a collector, or a vendor SDK.

`observability-log` may contain portable console and JSONL sinks. Platform log
services and vendor transports remain separate adapters.

`observability-profile` is part of the intended observability family but is not
required in the first version set. It should enter a later virtual-bundle
revision only after its collection overhead, symbolization, async-stack model,
interchange formats, and security boundaries are demonstrated.

## Virtual Package

The virtual package contains no runtime code and creates no new semantic type
identity. Its metadata binds exact compatible versions and supported external
specification profiles:

```text
virtual package observability 1.4 {
    provides observability-api        1.2
    provides observability-trace      1.4
    provides observability-metrics    1.3
    provides observability-log        1.1
    provides observability-transform  1.4
    provides observability-otel       1.5
    provides observability-otlp       1.5
    provides observability-prometheus 1.2
    provides observability-testing    1.3
}
```

A later bundle may additionally provide `observability-profile`. Adding it to
the version set makes it available; reachability and selective package rules
still prevent profiling code from entering programs that do not import or
configure it.

Exact metadata syntax belongs to the package-format design. Depending on the
virtual package makes the tested set available; it does not compile, link, or
download every listed package. Only packages imported by reachable source,
selected application configuration, or tests enter that build graph.

Projects may instead select individual packages. Lockfiles record the concrete
package versions and hashes, the bundle revision when used, and applicable
OpenTelemetry, OTLP, Prometheus, and OpenMetrics profiles. A bundle update may
select a new exporter without changing `observability-api`. Compiler upgrades
must not silently upgrade a locked bundle.

## Shared API and Context

`observability-api` defines the common vocabulary:

```text
TelemetryContext
Resource
InstrumentationScope
Attributes
AttributeValue
TraceId
SpanId
TraceFlags
```

Attribute values are bounded Boolean, integer, floating-point, text, or
homogeneous lists of those scalars. Arbitrary objects, nested maps, closures,
capabilities, pointers, and unbounded values are excluded. Collections have
deterministic iteration, duplicate-key diagnostics, and count and byte limits.

Signal packages provide valid no-op implementations. Libraries need no null
checks and no instrumented versus uninstrumented variants. When disabled, calls
should avoid allocation, clock access, formatting, projection, locking, context
copying, and exporter code.

Applications construct concrete providers and pass them to long-lived clients.
A global convenience provider is acceptable only with explicit installation,
replacement, shutdown, and test-isolation rules. Correctness cannot depend on
mutable global initialization order.

`TelemetryContext` follows logical execution rather than native threads. It
survives calls and suspension; structured child tasks derive it by documented
rules, while detached tasks choose explicitly. V1 may pass it explicitly until
the concurrency design admits task-local storage.

Propagators inject and extract context through typed carriers. HTTP support
includes W3C `traceparent` and `tracestate`; baggage is separately configured
and bounded. Invalid untrusted headers cannot trap a request, forge authority,
or grant capabilities.

Logs emitted in a span inherit trace and span identities. Metric exemplars may
refer to sampled traces. Trace IDs must never become ordinary metric labels.

Telemetry is observable behavior. A transform may not secretly instrument a
declared pure function while preserving a false effect contract. Initial
automatic instrumentation should target already effectful boundaries or APIs
whose contracts explicitly admit observation.

## Transformation Layer

`observability-transform` should generate instrumentation using the ordinary
[transformation layer](../../docs/language/MACRO.md). The compiler does not know
OpenTelemetry names or attach hidden behavior to filesystem or HTTP functions.

A declaration conceptually supplies a semantic operation and safe projection:

```text
@observability.operation(
    name: "filesystem.read_file",
    attributes: { "file.extension": path.extension() }
)
fn read_file(self, path: Path) Result[Bytes, FileError] {
    ...
}
```

Expansion checks whether recording is enabled, constructs admitted attributes,
starts the observation, invokes the body, maps success, error, cancellation,
and traps, and finishes during deterministic cleanup.

Transforms must:

- target resolved declaration or interface identities, not text names;
- preserve evaluation order, ownership, effects, cleanup, and cancellation;
- evaluate each projection once at its documented lifecycle point;
- avoid projection work when disabled;
- expose generated code and authored-to-generated provenance;
- enter their version and configuration into artifact identity;
- avoid making unused declarations reachable;
- diagnose duplicate instrumentation;
- require authors to declare safe attributes and completion semantics.

The transform cannot infer whether a URL is safe and low-cardinality, when a
stream completes, or which result represents a semantic failure.

## Known APIs and Semantic Interfaces

For known stable official APIs, such as filesystem operations, the transform
can target exact resolved declarations and provide standard conventions. This
is ordinary versioned library coupling, not compiler magic.

Replaceable HTTP, database, messaging, and RPC implementations expose or adapt
to small semantic interfaces. Their richer public APIs remain independent.

Conceptually:

```text
OperationStart {
    operation_id
    kind
    context
    safe_target
    attributes
}

OperationFinish {
    status
    portable_error
    bytes_sent
    bytes_received
    attributes
}
```

The observer owns monotonic timestamps and duration. The implementation marks
the semantic completion boundary. Optional fields and a bounded namespaced
extension collection permit evolution without adding positional arguments.

A concrete HTTP library may also accept timeouts, retry policies, proxies, TLS
configuration, pools, schedulers, and custom DNS. These stay private unless a
published convention or explicit safe projection makes one relevant. A timeout
outcome is relevant; the configured timeout duration normally is not.

Observers are read-only. Test interceptors may reuse stable operation IDs, but
their power to change outcomes remains a separate test-only capability.

## Filesystem Instrumentation

Filesystem functionality remains outside mandatory `core`. Its stable API can
provide semantic operations such as `open`, `read_file`, `write_file`, `copy`,
`metadata`, and `remove`.

A high-level operation may contain many platform calls:

```text
filesystem.read_file
    +-- platform.open
    +-- platform.read
    +-- platform.read
    +-- platform.close
```

Normal tracing emits the semantic operation. Low-level calls normally feed byte
counters, error counters, histograms, or sampled events. Per-call or per-syscall
spans require a diagnostic mode.

Default attributes may include operation, portable outcome, byte count, and a
bounded file category. They exclude contents, full paths, home directories,
usernames, temporary secrets, handles, and pointers. Native errors are optional
diagnostics; portable errors remain primary.

Opaque foreign libraries can bypass the platform layer. External eBPF,
`ptrace`, DTrace, ETW, or similar tools may add native observations without
replacing portable instrumentation.

## HTTP Instrumentation

The official packages define a small observation interface usable by different
client and server libraries, not the only HTTP API.

Client instrumentation:

1. creates a `CLIENT` span from current context;
2. records method, sanitized server and target, and protocol facts;
3. injects trace context into outgoing headers;
4. invokes the concrete transport;
5. records status, portable error, optional resend count, and bytes;
6. finishes at the documented response boundary.

Server instrumentation:

1. extracts untrusted incoming context;
2. creates a `SERVER` span associated with request work;
3. records the matched low-cardinality route after routing;
4. invokes the application handler;
5. records response status, portable error, and bytes;
6. finishes at the declared response boundary.

Span names and metric dimensions use `GET /users/{user_id}`, never concrete
paths such as `GET /users/847239`. Query strings, credentials, authorization
and cookie headers, and bodies are absent by default. Header capture requires a
bounded explicit allowlist.

The base interface records one logical operation. Libraries exposing retry or
redirect attempt boundaries may add standard attempt observations. Generic
instrumentation must not invent attempts. Streaming implementations document
whether spans end after headers, body consumption, or another stable boundary.

## Metrics and Prometheus

`observability-metrics` defines counters, up/down counters, histograms, and
observable gauges. Descriptors contain stable names, descriptions, units,
instrument kinds, numeric types, scope, and fixed attribute schemas where
possible. Incompatible duplicate descriptors are errors.

`observability-prometheus` reads this model and provides deterministic
Prometheus text and OpenMetrics exposition. It maps instruments, resources,
scopes, units, attributes, and exemplars using published compatibility rules.
Mapping collisions and unsupported features are diagnosed rather than silently
merged.

The scrape endpoint is an ordinary HTTP handler with application-supplied
listen and access capabilities. Creating a meter never opens a port. Exporting
OTLP to a collector that exposes Prometheus is equally valid.

Metric attributes have cardinality budgets. User IDs, emails, request IDs, raw
URLs, paths, messages, and stack traces are prohibited default labels. A series
limit applies a bounded drop or coalescing policy and increments a non-recursive
self-observation counter.

## Structured Logs

`observability-log` uses the OpenTelemetry record model: event and observed
timestamps, severity, event name, body, bounded attributes, resource, scope,
trace ID, span ID, and flags.

The API favors stable event names and typed attributes:

```text
logger.info(
    event: "configuration.loaded",
    attributes: {
        "configuration.source": "file",
        "configuration.entry_count": values.length()
    }
)
```

Human prose remains available as a body or rendered view. Formatting and
attribute expressions should not execute when disabled. Logging an error does
not propagate it or terminate the program.

Official sinks include deterministic JSONL and concise human output. Binary or
multiline bodies have explicit encoding and bounds. Platform and vendor sinks
are separate adapters.

## Profiling and Flame Graphs Later

Profiling should become a later independently versioned
`observability-profile` package. A flame graph is one visualization of sampled
profile data, not a separate telemetry signal or the canonical stored format.

The first profile prototype should concentrate on sampled CPU and wall-time
stacks. Allocation, retained-memory, lock-contention, task-wait, off-CPU, and
I/O profiles may follow only when their event definitions and overhead are
precise. Instrumentation-based profiling hooks and external platform sampling
are complementary:

```text
external sampling
    perf, eBPF, DTrace, ETW, or another target profiler

runtime sampling
    SlopH native stacks, logical tasks, allocations, locks, scheduler waits

profile model
    stacks + sample values + resource/scope/context + symbol identities

views and export
    folded stacks, flame graph, pprof-compatible data, later OTLP profiles
```

The package should initially preserve a stable internal profile model and
support loss-aware conversion to common formats. Folded stack text provides a
simple inspectable input for flame-graph renderers. A pprof-compatible profile
allows existing analysis tools. OpenTelemetry Profiles is currently an alpha
specification, so compatibility should be implemented behind an explicit
profile version rather than freezing its current schema into SlopH's stable
API.

The flame-graph renderer may produce deterministic SVG or data for an
interactive viewer. Rendering is separate from capture so the same profile can
produce top-function, call-tree, diff, and flame-graph views without sampling
the program again. Generated output records capture period, sample frequency,
sample kind and unit, target, executable and debug identities, symbolization
status, profiler version, dropped samples, and clock source.

Useful profiles require compiler cooperation for symbol and source maps, but
not new Core expression forms. Optimized code must account for inlining, tail
calls, merged functions, generated transformations, and stripped symbols.
Async profiling should distinguish native stacks from logical task ancestry;
the renderer must not invent synchronous frames where only a task relationship
is known.

Profile samples may link to trace and span identities when safely available,
allowing a hot stack to be associated with a request. Unsampled trace IDs are
not synthesized, and correlation metadata must not create an unbounded series
or expose tenant and request identities in default output.

Profiling is explicitly enabled and bounded. Sampling frequency, duration,
target tasks or threads, stack depth, unique stack count, retained bytes, and
symbolization work have limits. Attaching to another process or collecting
kernel stacks requires explicit platform authority. Profiles can reveal code
structure, paths, arguments, tenant activity, and secrets present in symbols or
labels, so capture, storage, rendering, and export follow the same redaction and
access policies as other telemetry.

The package needs conformance cases for recursion, inlining, tail calls,
foreign frames, unknown symbols, task migration, cancellation, concurrent
sampling, truncated stacks, deterministic folding, SVG escaping, profile
comparison, and trace correlation. Benchmarks measure disabled overhead,
sampling interruption cost, maximum sustainable frequency, lost samples,
symbolization time, retained profile memory, output size, and application
slowdown on the reference workloads.

## APM, Export, and Reliability

APM is the correlated view of traces, metrics, logs, resources, and later
profiles. One HTTP observation may feed a span, counter, histogram, error event,
and exemplar without several backends re-instrumenting the handler.

Application startup supplies clocks, resources, samplers, processors, limits,
and exporter capabilities. Signal APIs and dependencies do not acquire ambient
network, filesystem, environment, process, randomness, or secret authority.

`observability-otlp` makes endpoint, transport, security, authentication,
timeout, retry, batching, and queue limits explicit. Export happens outside the
observed operation where practical and is marked internal to prevent recursion.
Shutdown is bounded and reports undelivered records.

Every queue, batch, attribute set, body, baggage item, series registry, retry
schedule, and test history has count and byte limits. Default export failure
drops or retries within policy and does not fail the application operation. A
stricter audit policy is explicit and changes documented effects.

The SDK distinguishes sampling rejection, cardinality rejection, attribute
limits, full queues, retry expiry, encoding or export failure, shutdown expiry,
and recursion suppression. Self-observation cannot recursively invoke the same
failing path.

## Privacy

Safe defaults require:

- allowlisting sensitive arguments and headers;
- excluding bodies, contents, paths, command arguments, environment,
  credentials, cookies, tokens, and personal identifiers;
- using route templates and semantic targets rather than raw identifiers;
- deterministic redaction before processing and export;
- bounding values before serialization;
- preventing baggage from granting authority;
- authenticating and encrypting export according to application policy.

Projection code has ordinary review and expansion provenance. Names such as
`name`, `id`, `path`, or `token` never establish that a value is safe.

## Testing

`observability-testing` supplies a deterministic clock, bounded in-memory
provider, and semantic assertions for span trees, causal order, attributes,
metrics, logs, exemplars, drops, propagation, and shutdown.

It integrates with the
[bundled test framework](../TEST_FRAMEWORK.md). Fault plans cover exporter
connection failure, timeout, short write, malformed response, queue exhaustion,
clock anomaly, cancellation, shutdown deadline, and recursive instrumentation.

Exact global ordering is asserted only under a deterministic scheduler.
Concurrent recording preserves task identity and parentage. Test snapshots use
production redaction rules.

## AI-First Inspection

Inspection should show:

- authored and expanded instrumentation;
- semantic operation identity and lifecycle boundaries;
- attribute types, cardinality classes, and redaction policy;
- context injection and extraction points;
- concrete packages selected by the virtual bundle;
- active samplers, processors, exporters, queues, and limits;
- reasons records or series were dropped;
- schema, convention, protocol, and transform versions.

Diagnostics identify unsafe projections, duplicate descriptors, missing finish
paths, conflicting transforms, bundle conflicts, and unsupported mappings with
actionable repairs.

## Performance and Conformance

Benchmarks must cover disabled calls; sampled and unsampled spans; 0, 4, 16,
and maximum attributes; sync and async HTTP propagation; filesystem spans and
buffered reads; bounded metric series; concurrent producers; JSONL, OTLP,
Prometheus, and OpenMetrics encoding; failing exporters; flush and shutdown;
large transform expansions; individual package imports; virtual bundle imports;
and builds importing nothing from observability.

Report latency, CPU, allocations, peak memory, queue occupancy, drops, bytes,
throughput, executable size, compile time, and retained declarations. The
virtual bundle must not cause unused packages to be downloaded, parsed,
checked, compiled, or linked.

Conformance tests cover no-op APIs, task context, W3C propagation, HTTP and
filesystem lifecycle and errors, log correlation, metric rules, deterministic
exposition, exporter mappings, cardinality and queue limits, recursion,
redaction, transform correctness, virtual-bundle resolution, lock identity, and
absence of undeclared capabilities.

Quantitative budgets and a reference environment are required before stability.

## Versioning

Every concrete package has its own semantic version. The virtual bundle has a
separate version selecting exact compatible package versions. Releases record
the OpenTelemetry specification, semantic conventions, OTLP, Prometheus, and
OpenMetrics profiles implemented.

The application selects one compatible convention profile. Dependencies cannot
emit conflicting profiles for one operation. Explicit bounded migration modes
may duplicate old and new fields, but ordinary upgrades do not silently do so.
Saved test artifacts record concrete packages, bundle, schemas, conventions,
transforms, and exporters.

## Decisions Deferred to Prototypes

- Final concrete and virtual package names.
- Exact virtual-package metadata and selective availability rules.
- Attribute representation and static versus dynamic key APIs.
- Explicit context versus task-local convenience.
- Whether observation needs a dedicated effect.
- Initial sampling, histogram, queue, and batching defaults.
- OTLP transports and binding generation.
- Initial Prometheus and OpenMetrics feature profiles.
- Initial profiling sample kinds, platform collectors, symbolization contract,
  interchange formats, and flame-graph renderer.
- Which stable official APIs receive transforms first.

These choices may change spellings and implementations. They may not move
observability into mandatory Core, collapse packages into one build unit,
create a competing Prometheus API, leak rich implementation details into
semantic interfaces, weaken bounds, or make exporter availability ordinary
library correctness.

## References

- [OpenTelemetry specification](https://opentelemetry.io/docs/specs/otel/)
- [OpenTelemetry client design principles](https://opentelemetry.io/docs/specs/otel/library-guidelines/)
- [OpenTelemetry context](https://opentelemetry.io/docs/specs/otel/context/)
- [OpenTelemetry propagators](https://opentelemetry.io/docs/specs/otel/context/api-propagators/)
- [OpenTelemetry log data model](https://opentelemetry.io/docs/specs/otel/logs/data-model/)
- [OpenTelemetry semantic conventions](https://opentelemetry.io/docs/specs/semconv/)
- [OpenTelemetry HTTP spans](https://opentelemetry.io/docs/specs/semconv/http/http-spans/)
- [OpenTelemetry Prometheus compatibility](https://opentelemetry.io/docs/specs/otel/compatibility/prometheus_and_openmetrics/)
- [Prometheus client-library guidance](https://prometheus.io/docs/instrumenting/writing_clientlibs/)
- [Prometheus metric and label naming](https://prometheus.io/docs/practices/naming/)
- [Prometheus exposition formats](https://prometheus.io/docs/instrumenting/exposition_formats/)
- [OpenTelemetry Profiles](https://opentelemetry.io/docs/specs/otel/profiles/)
- [Brendan Gregg's FlameGraph tools](https://github.com/brendangregg/FlameGraph)
- [pprof profile format and tools](https://github.com/google/pprof)

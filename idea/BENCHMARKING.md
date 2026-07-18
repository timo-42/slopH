# IDEA: Bundled Benchmark Runner and Compiler Regression Suite

Status: exploratory, non-normative.

SlopH should ship a benchmark runner and must maintain a benchmark suite for
the toolchain itself. Fast compilation is a governing product requirement, so
performance cannot be validated by occasional manual measurements or a few
small microbenchmarks.

These are related but distinct deliverables:

1. `sloph bench` is a bundled facility with which package authors measure their
   own code.
2. The compiler regression suite is a versioned collection of workloads,
   edit scenarios, environments, budgets, and saved results used to keep the
   language, Core, compiler, package manager, linker, and runtime fast.

The benchmark facility belongs in the batteries shipped with the toolchain. It
is not a primitive of the source language or canonical Core, and ordinary
production programs must not depend on it. Small compiler-recognized barriers
may nevertheless be required to stop an optimizer from deleting or moving the
work being measured.

The initial implementation may support only the compiler suite. The public
runner should later use the same measurement engine and result schema rather
than growing a second incompatible system.

## Tests and Benchmarks Have Different Semantics

A test establishes a correctness claim and normally has a deterministic pass
or fail result. A benchmark estimates a quantity in a noisy environment.
Passing once does not establish a performance result, and a slower observation
does not by itself establish a regression.

Benchmark discovery, selection, fixtures, parameter cases, process isolation,
and structured diagnostics may share infrastructure with the eventual bundled
test framework. Measurement, sampling, comparison, and regression policy must
remain separate.

By default, a benchmark run reports measurements but succeeds even when the
current result is slower than a saved result. It fails only when the invocation
or project declares a compatible baseline and an explicit budget or regression
policy. A benchmark whose result is incorrect is an error; benchmarks do not
replace correctness tests.

## Proposed User Model

The exact source syntax is deliberately provisional. Conceptually, a benchmark
has a stable name, zero or more typed parameter cases, setup outside the timed
region, and a measured operation:

```text
benchmark "map/1000 integers" {
    values = fixture.integer_list(count: 1000)

    measure {
        result = map(values, increment)
        bench.observe(result)
    }
}
```

The runner owns the iteration loop. It first estimates the operation's cost,
warms it up when the selected scenario calls for warm state, chooses a bounded
sample plan, and records complete samples. User code must not normally guess an
iteration count.

The library should support:

- named benchmark groups and stable benchmark identities;
- typed parameter cases with readable, deterministic case names;
- fixtures whose suite, group, sample, or iteration lifetime is explicit;
- setup and cleanup excluded from measurement by default;
- an explicit measured setup mode for end-to-end operations;
- latency, throughput, batch, concurrent, and externally timed operations;
- bytes, items, requests, or another declared unit per operation;
- allocation, peak-memory, artifact-size, and user-defined counters;
- subprocess and command benchmarks with explicit inputs and environment;
- asynchronous operations without measuring unrelated executor startup unless
  requested by the scenario;
- a cheap dry run that validates every selected benchmark once;
- deterministic selection by package, path, group, name, tag, and case;
- human output and a stable versioned machine-readable result artifact.

The runner must reject duplicate benchmark identities, ambiguous selections,
invalid lifetimes, unsupported counters, and a measured block that never
executes. A parameter value's debug rendering is not a stable identity unless
the declaration explicitly makes it one.

## Measurement Boundaries

Benchmark code is vulnerable to measuring the wrong operation. The framework
must make the boundary visible and hard to misuse.

`bench.observe(value)` conceptually makes a result escape to the harness so
dead-code elimination cannot discard it merely because the source program does
not otherwise consume it. A separate memory barrier may be necessary for code
whose relevant result is a mutation. These operations are benchmark-only
compiler services with tightly specified optimization semantics, not general
reflection or arbitrary optimizer control.

The compiler may still optimize the operation itself and may constant-fold a
known computation. That is usually correct: the benchmark measures code as it
would be optimized in a program. A benchmark needing runtime input must create
that input outside the measured region and pass it through an appropriate
opaque input operation.

The runner must measure and publish its empty-region and loop overhead. It may
batch very fast operations, but reports how the batch count was selected and
must not claim precision below the effective timer and harness resolution.
Setup, cleanup, fixture creation, allocation, compiler startup, process startup,
and cache population are excluded only when the benchmark explicitly places
them outside its scenario.

## Sampling and Comparison

Wall-clock output from one execution is not sufficient. For each measured
quantity the result records all retained samples, sample iteration counts,
warmup and calibration observations, and rejected or failed samples.

The default statistical report should contain at least:

- a robust central estimate such as the median;
- dispersion such as median absolute deviation;
- a confidence interval when its assumptions and sample count permit one;
- minimum and maximum as observations, not as the primary estimate;
- the before/after absolute change and ratio;
- the comparison method and configured practical-change threshold;
- warnings for excessive noise, clock resolution, throttling, or too few
  useful samples.

The project should select and document one well-reviewed comparison method
before results become release gates. Statistical significance alone is not a
useful regression rule. A gate requires both adequate evidence and a
predeclared practically meaningful effect size. Re-running a noisy comparison
until it fails or passes is prohibited process, not additional evidence.

Before and after measurements should be randomly interleaved when practical.
Otherwise machine temperature, battery state, background load, and other drift
can be confused with the code change. The raw ordering remains present in the
result artifact.

Some metrics are exact or nearly deterministic, including emitted declaration
count, cache invalidations, artifact bytes, and compiler work counters. They do
not need pretend statistical treatment. Each metric declares whether it is
sampled, monotonic, or exact and which comparison policy applies.

## Environment Identity

Every saved result includes an environment fingerprint. At minimum it records:

- benchmark-suite revision and workload content hash;
- source revision and dirty-state indicator;
- compiler build, language profile, Core version, package lock, and runtime;
- target triple, CPU features, backend, optimization profile, and linker;
- operating system, kernel, architecture, logical CPU count, and memory;
- CPU model and available frequency, power, affinity, and thermal information;
- worker count and all performance-relevant compiler options;
- cache mode and the exact initial cache state;
- command line, selected cases, clock, sampling policy, and random seed;
- declared environment values and external tools that influence the workload;
- start time, duration, failures, warnings, and runner version.

Machine-specific paths and secrets must not leak into a shareable result. The
schema should distinguish an omitted value from an unavailable observation.

Comparison first checks compatibility. Results from different workloads,
targets, optimization modes, cache scenarios, or materially different machines
are rejected by default. An explicit exploratory override may display them but
must label the comparison as uncontrolled and may not satisfy a release gate.

The result artifact is immutable and content-addressed. A friendly baseline
name points to an artifact; it is not the artifact identity. The runner never
silently updates a baseline after a successful run.

## Compiler Regression Suite

The mandatory compiler suite implements the performance requirements in
[INFRASTRUCTURE.md](../docs/toolchain/INFRASTRUCTURE.md). It is checked into the
repository as generators, canonical seeds, edit descriptions, and expected
structural properties. Large generated trees need not be committed when they
can be reproduced deterministically and cheaply.

The reference project contains approximately 200,000 lines of realistic code
and 150 dependencies. It must exercise modules, interfaces, generics, macros,
effects when present, generated code, package lookup, native code generation,
and linking. Repeated filler declarations are useful for controlled scaling
experiments but do not substitute for the representative workload.

Each build benchmark starts from a declared filesystem, registry, artifact,
and cache state. Measuring a command after the harness already populated the
cache is not a cold-build benchmark. Compiler scenarios should run in fresh,
isolated work areas and retain enough of the build report to explain why work
occurred.

The suite must include at least:

### Build and Edit Loops

- canonical-source bootstrap with an empty download and artifact cache;
- clean source-only check and native build;
- clean builds with each supported registry accelerator independently enabled;
- a no-change check and build;
- a private leaf-body edit;
- an unused private declaration edit;
- an edit to an unreachable function body;
- adding and removing an unused package dependency;
- a public-interface edit at the leaf, middle, and root of a deep graph;
- macro input and generated-source changes;
- standard-library and Core compatibility invalidation;
- several builds in independent worktrees running concurrently;
- deterministic output under different paths, worker counts, and schedules.

The edit scenarios record files and semantic declarations changed, expected
invalidation boundaries, expected reachability differences, and whether the
final program behavior or artifact identity should change.

### Reachability and Dependency Pruning

The breadth and depth series in
[REACHABILITY_COMPILATION.md](./REACHABILITY_COMPILATION.md) must be part of the
suite. In particular, a data structure with many independent methods must show
that using `push` does not parse, check, specialize, optimize, or emit `size`
and helpers reachable only from `size` in the selected build mode.

Measurements include discovery work as well as saved backend work. A reachability
implementation that spends more time constructing its pruning graph than eager
compilation saved must be visible. Correctness runs still complete-check every
package configuration on its declared schedule so unreachable stale errors are
not hidden indefinitely.

### Generics

The generic shape, breadth, depth, and edit series required by
[GENERICS.md](./GENERICS.md) are first-class benchmark cases. The suite compares
parametric Core, bounded pre-Core instantiation, and erasure prototypes when
those alternatives exist. It reports body checks, applications, witness
lookups, layout work, specializations, cache reuse, emitted code size, wall
time, and peak memory.

No generic implementation may be accepted because its runtime microbenchmark
is fast while clean or incremental compile time grows unpredictably. The
ordinary development profile is measured with optional specialization and
whole-program optimization disabled as well as enabled.

### Compiler Phases and Resources

Every scenario records total wall and CPU time, peak resident memory, bytes
read and written, artifact sizes, cache hits and misses, and bounded work
counters for applicable phases. At minimum the compiler exposes discovery,
parsing, expansion, name resolution, elaboration, Core validation,
optimization, code generation, and linking separately.

Phase counters are part of performance correctness. For example, a private
body edit with an unchanged interface should report zero dependent modules
rechecked even on a noisy CI machine where elapsed time cannot reliably prove
that property.

### Runtime and Output Quality

Compile speed is primary but cannot be purchased through unusable output. A
small representative runtime suite records startup latency, steady-state
latency and throughput, peak memory, allocation counts where supported,
executable size, and generated-code size.

It covers collections, text and binary processing, allocation-heavy code,
generic dispatch, error paths, filesystem and network abstractions using local
controlled peers, concurrency when available, and at least one complete
application. Runtime results are compared against SlopH baselines first;
external-language comparisons require published equivalent source and build
settings and are informational unless a separate policy says otherwise.

## CI and Release Policy

Ordinary shared CI machines are suitable for correctness, schema validation,
determinism, exact work counters, broad catastrophic-regression limits, and
dry-running all benchmark cases. They are not automatically suitable for
detecting small wall-clock changes.

Three gate levels should be used:

1. Every change runs benchmark conformance tests and exact structural budgets.
2. A controlled performance worker runs repeated before/after comparisons for
   relevant changes and publishes the complete result.
3. Release candidates run the entire suite on the documented reference
   machine and must satisfy absolute latency, memory, and artifact-size budgets.

Changes to the benchmark, workload, runner, reference machine, or budget are
reviewed independently from a compiler change that benefits from them whenever
practical. A pull request may not make its regression disappear by updating
its own baseline. Intentional budget changes require a rationale and preserve
the previous result for comparison.

The suite should select affected cases from explicit compiler-component and
workload metadata, but a scheduled complete run guards against incorrect
selection. Failure output identifies the metric, baseline, current result,
effect size, evidence, environment differences, and a command or artifact with
which to investigate it.

## AI-First Requirements

Machine-readable results are the primary integration contract, not scraped
terminal tables. Stable identities allow an agent to compare the same case
across commits without guessing from display names.

A result should answer:

- what became slower, larger, or more memory intensive;
- whether the change is statistically credible and practically meaningful;
- which compiler phase or exact work counter changed;
- which declarations, dependencies, or invalidations caused the extra work;
- whether the environment or benchmark definition also changed;
- which narrower benchmark or inspection command should be run next.

Benchmark declarations favor explicit inputs, fixture lifetimes, units, and
measurement boundaries over implicit module-global state. Generated benchmark
cases must be reviewable and bounded. The runner emits concise diagnostics for
common mistakes such as optimizing all work away, including setup accidentally,
sharing mutable state between samples, or comparing incompatible results.

## Runner Conformance and Performance

The benchmark system itself requires tests. They cover:

- stable discovery and identity under file and declaration reordering;
- exact fixture lifetime and setup/cleanup inclusion rules;
- timer conversion, overflow, resolution, and monotonicity handling;
- calibration and sample-plan bounds for extremely fast and slow operations;
- optimizer barriers using generated-code and observable-side-effect checks;
- subprocess exit, timeout, signal, output, and cleanup behavior;
- statistical comparison against fixed synthetic datasets;
- incompatible environment and schema rejection;
- complete round trips of the structured result format;
- interruption without promoting a partial result to a valid baseline;
- discovery and dry-run overhead on large packages;
- benchmark selection compiling only selected entry points where reachability
  mode permits it.

Synthetic clock and process services should make harness logic deterministic
in tests. A smaller set of end-to-end cases validates the real monotonic clock,
process isolation, optimizer barriers, and platform metadata.

## Provisional Commands

The public spelling remains deferred until the package and test commands are
designed together. The intended capabilities are approximately:

```text
sloph bench [SELECTION]
    [--dry-run]
    [--save NAME]
    [--compare NAME_OR_RESULT]
    [--format human|json]
    [--output PATH]
    [--warmup DURATION]
    [--measurement-time DURATION]
    [--samples COUNT]
    [--seed INTEGER]

sloph bench compare BEFORE AFTER
sloph bench inspect RESULT
```

Project defaults may cap duration and select counters, but a committed project
configuration may not silently lower machine-wide release standards. Commands
that mutate a friendly baseline name must be explicit and separate from an
ordinary measurement.

## Design Precedents

This proposal deliberately combines established ideas instead of inventing a
novel measurement model:

- [Criterion.rs analysis](https://bheisler.github.io/criterion.rs/book/analysis.html)
  separates warmup, measurement, statistical analysis, and baseline comparison.
- [Go benchstat](https://pkg.go.dev/golang.org/x/perf/cmd/benchstat) uses robust
  comparison by default, distinguishes exact metrics, and recommends
  interleaving before and after runs to reduce drift.
- [Google Benchmark](https://google.github.io/benchmark/user_guide.html)
  demonstrates explicit optimizer barriers, repetitions, custom counters,
  structured output, fixtures, and concurrent benchmarks.

The eventual SlopH algorithms and defaults still require independent review and
conformance tests. Compatibility with these tools is not a design goal.

## Decisions Deferred to Measurement

- the exact source syntax and fixture integration;
- the sampling and confidence-interval algorithms;
- default sample counts, warmup, duration, and noise thresholds;
- process-per-group, process-per-benchmark, or reusable-worker isolation;
- supported hardware performance counters and privilege model;
- whether CPU pinning or machine tuning is automated or only diagnosed;
- the result encoding in addition to required versioned JSON interchange;
- storage and visualization of long-term history;
- reference machine, absolute budgets, and allowed replacement procedure;
- which tiny set of optimizer barriers must be compiler-recognized.

These decisions may change the implementation, but they may not weaken raw
sample retention, environment identity, explicit baselines, cold-state
reproducibility, structural compiler counters, or the requirement that SlopH's
own performance be continuously checked.

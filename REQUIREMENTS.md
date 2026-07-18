# Top-Level Requirements

These requirements override future minor goals. A proposed feature may be
rejected if it materially harms any of them.

1. **AI-first authoring**
   - Code must be easy for AI systems to generate, modify, and reason about.
   - Prefer uniform rules, explicit behavior, and minimal special cases.

2. **AI-readable**
   - Syntax and semantics must minimize ambiguity.
   - Relevant behavior should be locally understandable wherever practical.

3. **Human-readable**
   - Humans must be able to review, debug, and maintain generated code.
   - Easy manual authoring is not required.

4. **Small complete language**
   - The entire language specification and essential documentation must fit
     within an AI context window.
   - Every feature has a documentation and conceptual-complexity cost.
   - Convenience features may be omitted when they increase language size
     disproportionately.

5. **Fast compilation and disposable feedback loops**
   - Compilation latency is a primary performance requirement, measured as part
     of the complete edit, check, test, and run cycle.
   - Cold, average, incremental, and dependency-invalidating builds are all
     first-class cases. The language must not be designed around a perfect-cache
     best case.
   - Multiple agents must be able to build independent worktrees concurrently
     without disproportionate setup time, memory use, or shared-cache machinery.
   - A clean project must remain acceptably buildable without remote builders,
     elaborate environment managers, or previously populated caches. Caching is
     an acceleration, not a prerequisite for a usable toolchain.
   - Type-system, macro, optimization, and package features may be rejected when
     their compile-time or setup cost outweighs their demonstrated benefit.
   - Compiler diagnostics must be deterministic, actionable, and suitable for
     an AI repair loop.

6. **General-purpose**
   - The language should support ordinary software such as command-line tools,
     services, and libraries.
   - It is not initially specialized for agent workflows or ML kernels.

7. **Native output**
   - The first implementation should produce native executables.

8. **Modular organization**
   - The language must support modules or namespaces for organizing code,
     controlling visibility, resolving global names, and enabling separate
     compilation.
   - The module system must remain compatible with explicit compile-time syntax
     dependencies.

The compiler, build system, package registry, artifact, cache, trust, and
performance requirements derived from these goals are specified in
[INFRASTRUCTURE.md](./INFRASTRUCTURE.md).

## Top-Level Non-Goal

- **Easy for humans to write manually.** Human ergonomics remain valuable only
  when they also improve readability or AI authoring.

## Admission Rule for Minor Requirements

A minor requirement or language feature should be accepted only when:

- it supports a concrete general-purpose use case;
- it preserves predictable AI generation and comprehension;
- its value justifies its specification and implementation size;
- it does not materially damage compilation speed.

## Production Warning: Scarf's Haskell Experience

Source: Avi Press,
[After 7 years in production, Scarf has reluctantly moved away from Haskell](https://avi.press/posts/2026-07-10-after-7-years-in-production-scarf-has-reluctantly-moved-away-from-haskell.html)
(July 10, 2026).

The full article is linked rather than copied. The following is an original
summary of the author's report, not a reproduction of the article and not a
controlled comparative study.

Scarf used Haskell for its production backend for approximately seven years,
including API and high-volume package-download services with uptime obligations.
Press reports that important Haskell promises held: the code was reliable, the
type system caught real bugs, domain modeling benefited from the language, and
high-performance services were achievable.

The reported costs were primarily compilation latency and ecosystem friction.
The team invested substantial effort in build optimization, caches, Nix,
developer environments, and CI. AI-assisted development changed the economics
of those costs: when an agent can propose a change quickly, a long cold build
can dominate the entire task. The cost multiplies when several agents explore
independent worktrees, and best-case cached incremental builds do not represent
cold starts or changes deep in the dependency graph.

Scarf consequently began placing new API work in Python and incrementally
migrating touched functionality while existing Haskell services continued to
run. Press reports faster development feedback, more extensive generated tests,
less toolchain work, and no concrete loss from reduced type safety observed by
the team at the time of writing. These are the author's production observations,
not proof that dynamic typing is generally safer or faster.

The failure mode this project must avoid is therefore:

> A language that produces reliable programs but makes compilation, project
> bootstrap, isolated experimentation, or toolchain maintenance the dominant
> cost of AI-assisted development.

Derived requirements:

- measure complete agent feedback latency rather than only warm incremental
  compiler latency;
- benchmark clean builds, deep dependency changes, and several concurrent
  isolated builds;
- keep the type system valuable but small enough to check quickly;
- prefer a few bounded deterministic compiler passes over heuristic repeated
  optimization;
- make project bootstrap reproducible with little configuration;
- provide realistic, copyable library examples in addition to type signatures;
- ensure diagnostics help an agent converge instead of merely rejecting code;
- treat cache configuration, environment management, and CI complexity as
  language/toolchain costs rather than external concerns;
- retain static guarantees only when their value justifies their feedback-loop
  cost under the top-level admission rule.

## Open Design Decisions

Runtime performance, memory management, syntax, type-system details,
interoperability, concurrency, target platforms, and the concrete module system
remain undecided. Module naming, file mapping, imports, exports, visibility,
re-exports, dependency cycles, package identity, and namespace separation will
be designed later. In particular, garbage collection, reference counting, and
simplified ownership are still open alternatives.

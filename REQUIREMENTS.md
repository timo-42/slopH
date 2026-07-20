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
   - Local and CI builds may cache or distribute versioned binary intermediate
     representations when validation and loading are cheaper than recomputation.
     The shipped toolchain must be able to validate each supported semantic
     intermediate and render it back to a deterministic canonical text form.
   - Every package published to a conforming registry must include both
     canonical source and its validated, versioned binary Syntax encoding. The
     registry must reject an upload when either representation is missing or
     they do not represent the same source program.
   - Type-system, macro, optimization, and package features may be rejected when
     their compile-time or setup cost outweighs their demonstrated benefit.
   - Language and compiler design must optimize measured end-to-end latency,
     not one phase in isolation. Parsing, expansion, name resolution, type
     checking, lowering, optimization, code generation, and linking must each
     remain observable and proportionate to their value.
   - Compiler diagnostics must be deterministic, actionable, and suitable for
     an AI repair loop.

6. **General-purpose**
   - The language should support ordinary software such as command-line tools,
     services, and libraries.
   - It is not initially specialized for agent workflows or ML kernels.

7. **Native output**
   - The first implementation should produce native executables.
   - The compiler architecture must also permit future managed targets built on
     established virtual machines without changing language semantics.

8. **Modular organization**
   - The language must support modules or namespaces for organizing code,
     controlling visibility, resolving global names, and enabling separate
     compilation.
   - The module system must remain compatible with explicit compile-time syntax
     dependencies.
   - Every source file's semantic dependencies must be explicit and discoverable
     from that file and its manifest. Parsing or checking one module may load
     compact declared interfaces, but must not require parsing, evaluating, or
     searching unrelated dependency implementations.
   - Imports do not acquire hidden transitive names, syntax, options, or build
     behavior. A module's effective compilation inputs must remain enumerable.

9. **Reproducible by construction**
   - Identical canonical source, dependencies, declared build inputs, toolchain
     versions, options, and target must produce byte-for-byte identical semantic
     interfaces, intermediate artifacts, generated code, and final artifacts.
   - Timestamps, absolute paths, directory order, worker scheduling, cache state,
     network availability, and other undeclared ambient state must not affect a
     build result.
   - Clean offline source builds are authoritative. Local caches, CI artifacts,
     registries, and remote builders may change latency only, never meaning or
     output.
   - Reproducibility must be continuously tested across clean directories,
     worker counts, cache states, and supported hosts where the declared target
     and toolchain are the same.

The compiler, build system, package registry, artifact, cache, trust, and
performance requirements derived from these goals are specified in
[INFRASTRUCTURE.md](./docs/toolchain/INFRASTRUCTURE.md).

## Top-Level Non-Goal

- **Easy for humans to write manually.** Human ergonomics remain valuable only
  when they also improve readability or AI authoring.

## Admission Rule for Minor Requirements

A minor requirement or language feature should be accepted only when:

- it supports a concrete general-purpose use case;
- it preserves predictable AI generation and comprehension;
- its value justifies its specification and implementation size;
- it does not materially damage compilation speed;
- it preserves reproducibility from fully declared inputs.

## Fast Compiler Pipeline Hints

The language should be selected and evolved for a fast complete compilation
pipeline. Parser simplicity remains valuable for predictability, diagnostics,
and implementation size even when parsing is not the dominant measured phase.
These are design heuristics rather than commitments to particular algorithms:

- prefer a deterministic context-free grammar with bounded lookahead and no
  dependence on semantic information or extensive backtracking during parsing;
- make expression precedence and associativity fixed and structurally
  unambiguous;
- use predictable lexical boundaries, unique keywords, and explicit delimiters
  or statement terminators where they avoid contextual guessing;
- make a module's grammar depend only on the language profile and explicitly
  declared, versioned syntax interfaces, never on dependency implementation
  bodies or the result of evaluating arbitrary code;
- keep the mapping from parsed syntax to AST nodes direct and inexpensive, and
  require syntactic sugar to justify its parsing and lowering cost;
- bound syntax nesting, parser stack use, token counts, and AST size rather than
  requiring programs to have an artificially flat scope hierarchy;
- prefer type rules that support module-local, deterministic checking with
  bounded inference and constraint solving; require annotations at boundaries
  where they avoid expensive global inference or repeated specialization;
- resolve every name and operator to one declaration without overload-candidate
  ranking, backtracking, or chains of implicit conversions;
- compile generic definitions independently using uniform representation and
  explicit dictionaries or witnesses by default; specialization must be
  optional, bounded, cached, and justified by release-profile measurements;
- lower early to compact, validation-friendly intermediate forms that support
  separate compilation and do not require repeated whole-tree reconstruction;
- prefer a few bounded streaming or direct-lowering passes and fuse phases when
  measurement shows a net benefit, without requiring literal single-pass
  compilation or sacrificing validation and diagnostics;
- use a small, deterministic optimization pipeline by default, and admit an
  optimization only when its compile-time cost and generated-code benefit are
  measured on representative workloads;
- measure tokenization, parsing, expansion, resolution, type checking, lowering,
  optimization, code generation, linking, allocation, and peak memory both
  separately and as one edit-to-result latency.

LL(1), LR(1), and similar deterministic grammar styles are useful reference
points, not requirements. An implementation also need not construct a concrete
syntax tree before producing an AST. The governing requirement is predictable,
bounded end-to-end compilation cost. No fixed claim about which phase dominates
may replace measurements of this language and its representative workloads.

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

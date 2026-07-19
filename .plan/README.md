# SlopH Milestone Roadmap

This directory tracks the outcomes required for SlopH to become a production,
self-hosted language. It sits above the detailed language, toolchain, and
research documents. Milestones say **what must be true**; they do not duplicate
feature specifications or implementation task lists.

The governing requirements remain [REQUIREMENTS.md](../REQUIREMENTS.md). If a
milestone conflicts with those requirements, the milestone must change.

## Definition of a Proper Language

For this roadmap, SlopH is a proper language when milestone 10 is complete:

- the V1 source language and canonical Core have stable, compact semantics;
- ordinary command-line tools, services, and libraries can be written without
  private compiler facilities or host-language helper code;
- clean and incremental builds satisfy the feedback-latency requirements;
- observable behavior is defined by an implementation-neutral conformance
  corpus shared by every compiler;
- the supported compiler is written in SlopH and can rebuild itself; and
- the complete distribution can be built and tested locally without a network
  or previously populated cache.

Reproducibly reducing the trust root through the proposed C90/RV32IM bootstrap
is a later roadmap. Self-hosting does not by itself claim a fully bootstrapped
trust chain.

## Milestones

| ID | Milestone | Depends on | Status |
|---:|---|---|---|
| 00 | [Product definition](00-product-definition/GOAL.md) | — | complete |
| 01 | Portable conformance foundation | 00 | proposed |
| 02 | Language kernel | 00, 01 | proposed |
| 03 | Types and abstraction | 02 | proposed |
| 04 | Effects, memory, and systems | 03 | proposed |
| 05 | Compile-time language | 03, 04 | proposed |
| 06 | Modules, builds, and packages | 03, 05 | proposed |
| 07 | Runtime and standard library | 03–06 | proposed |
| 08 | Production compiler | 01–07 | proposed |
| 09 | Tooling, conformance, and hardening | 01, 06–08 | proposed |
| 10 | Self-hosting and stable V1 | 08, 09 | proposed |

The `GOAL.md` and `PLAN.md` files for milestones 01–10 were lost from the
working tree before they were committed; their rows above retain the roadmap
table until those documents are recreated. Dependencies are completion gates,
not a prohibition on exploratory work. Work may overlap when the contracts it
consumes are sufficiently stable.

## Status

- `proposed`: the goal exists but has not been selected as active work.
- `active`: implementation and evidence collection are underway.
- `blocked`: a named external decision or dependency prevents meaningful work.
- `complete`: every exit gate is satisfied and linked evidence exists.

Only evidence changes a milestone to `complete`. An implementation that lacks
portable tests, a specification, diagnostics, or required measurements is
unfinished.

## Completion Rules

Every milestone must satisfy the following cross-cutting rules in addition to
its own exit gates:

1. **Specification:** Accepted and rejected behavior is documented at the
   supported public boundary.
2. **Implementation:** The supported compiler implements the declared profile
   without undocumented host-language shortcuts.
3. **Portable tests:** Observable behavior is represented in the external
   corpus under [`tests/`](../tests/).
4. **Diagnostics:** Failures are deterministic, actionable, and carry stable
   machine-readable identities.
5. **Limits:** Work, memory, recursion, and generated output are bounded where
   user input could otherwise make compilation unbounded.
6. **Performance:** Relevant cold, incremental, invalidating, and concurrent
   measurements remain within the accepted budgets.
7. **Documentation:** Examples are executable and the normative documentation
   remains within the project’s context-size goal.

All implementation agents must follow the shared
[execution instructions](EXECUTION.md). Each milestone's `PLAN.md` turns its
goal into ordered work and evidence-producing gates.

## Milestone Files

Each milestone contains `GOAL.md` with its outcome and `PLAN.md` with the
decision-complete implementation sequence. Add these additional files only when
they have real content:

- `DECISIONS.md` for accepted decisions and rejected alternatives; and
- `EVIDENCE.md` for test, benchmark, audit, and release evidence.

Do not use milestone files as a second issue tracker. Concrete tasks should be
derived from an active milestone and linked back to its exit gate.

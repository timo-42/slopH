# Milestone Execution Instructions

These rules apply to every milestone plan. They are written so an implementation
agent can make progress without inventing product policy or silently weakening
the acceptance gates.

## Authority Order

When instructions disagree, use this order:

1. [`REQUIREMENTS.md`](../REQUIREMENTS.md)
2. The active milestone's `GOAL.md`
3. Accepted decisions in the active milestone's `DECISIONS.md`
4. The active milestone's `PLAN.md`
5. Normative documents identified by milestone 00
6. Existing implementation behavior and tests
7. Proposed and research documents

Do not preserve an implementation behavior merely because it exists. Do not
change a public behavior merely because a proposed document suggests another
one.

## Start-of-Work Procedure

Before editing code:

1. Read `REQUIREMENTS.md`, this file, the milestone `GOAL.md` and `PLAN.md`, and
   every dependency milestone's `EVIDENCE.md` if present.
2. Run `git status --short`. Existing modifications belong to the user unless
   the active task explicitly identifies them. Never discard or overwrite them.
3. Run the narrow baseline tests for the component and the authoritative C11
   compiler suite when practical:

   ```text
   make test
   make cases
   make smoke
   ```

4. Record pre-existing failures. Do not claim that the milestone introduced or
   fixed them without evidence.
5. Inspect the relevant implementation and specifications with `rg`; do not
   create a second subsystem before confirming that no suitable subsystem
   exists.
6. Create `DECISIONS.md` only when the plan calls for a decision checkpoint.
   Product choices that the plan does not settle must be approved before
   implementation proceeds past that checkpoint.

## Change Procedure

Implement one externally testable slice at a time:

1. Update or add the normative contract.
2. Add portable accepted and rejected cases under `tests/`.
3. Implement the smallest compiler/library change that satisfies the cases.
4. Add implementation-private tests only for private algorithms or structures.
5. Run the narrow suite, then the portable suite, then the full suite.
6. Inspect canonical Syntax/Core/artifacts when the change crosses a compiler
   boundary.
7. Measure the relevant cost before and after any change to parsing, inference,
   expansion, specialization, dependency invalidation, or code generation.

Do not batch unrelated semantics into one change. A failing slice must be fixed
or cleanly reverted before starting the next one.

## Public Behavior Rules

- Every accepted construct needs at least one portable success case.
- Every new restriction needs a portable rejection case with a stable diagnostic
  code and relevant source span.
- Every limit needs cases immediately below and above the boundary.
- Every canonical representation needs parse/print/parse idempotence tests.
- Every lowering needs a source-to-stable-boundary case and an end-to-end case.
- Every backend behavior needs comparison with the reference evaluator where
  the evaluator supports the behavior.
- Every platform-specific behavior needs availability diagnostics on unsupported
  targets.
- Test expectations must not mention host-language class/structure names, private optimizer
  passes, temporary file names, addresses, clocks, or nondeterministic ordering.

## Portable Versus Private Tests

Put a test in the external corpus when another conforming compiler must produce
the same result. Put a test beside an implementation only when it inspects a
private algorithm, cache, object representation, host adapter, or fuzz driver.

If uncertain, default to a portable test. A feature cannot pass a milestone gate
when its only proof is an implementation-private test.

## Performance and Limit Rules

- Use clean, warm incremental, deep dependency invalidation, and concurrent
  isolated builds when the milestone affects compilation.
- Report wall time, peak memory, input size, generated size, and relevant work
  counters. A cache hit alone is not evidence of acceptable compilation.
- User-controlled recursion, expansion, evaluation, generated output, decoding,
  and graph traversal must have explicit limits and deterministic errors.
- A limit must be shared across nested work where resetting it would permit an
  input to evade the bound.
- Do not introduce an unbounded fixed-point loop, speculative grammar search,
  global typeclass search, or mandatory whole-program specialization.

## Evidence Procedure

Create or update the milestone's `EVIDENCE.md` as gates are proven. For each
gate record:

- the gate text;
- the implementation commit or working-tree description;
- the normative documents changed;
- the portable case directories;
- commands executed and their outcomes;
- benchmark inputs and results where applicable; and
- remaining limitations.

Do not mark a milestone complete because all planned code was written. Mark it
complete only when every checkbox in `PLAN.md` has evidence and every exit gate
in `GOAL.md` is satisfied.

## Stop Conditions

Stop and request a decision when:

- two normative documents require incompatible public behavior;
- a required product choice is still explicitly open;
- satisfying a task would violate a top-level requirement;
- the only apparent implementation requires unbounded or nondeterministic work;
- an existing user change overlaps the same code and intent cannot be inferred;
- a security boundary needs authority not granted by the milestone; or
- measured costs exceed the accepted budget and no bounded alternative remains.

Do not use a stop condition to avoid ordinary implementation work. First exhaust
the bounded, deterministic approaches already selected by the plan.

## End-of-Work Procedure

1. Run `git diff --check` and inspect the complete diff.
2. Run every affected portable profile and implementation-private suite.
3. Run the full bootstrap suite unless a documented environment limitation
   prevents it.
4. Validate local Markdown links and canonical generated files.
5. Update `EVIDENCE.md`; do not edit the milestone status without evidence for
   every gate.
6. Hand off with exact files changed, tests run, measurements, and remaining
   gates. Do not report future work as completed.

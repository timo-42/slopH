# Milestone 00 Execution Plan

Read [the goal](GOAL.md) and the shared [execution instructions](../EXECUTION.md)
before beginning.

## Fixed Result

Produce one authoritative product contract and an inventory that tells later
agents which documents are binding. Do not redesign individual language
features in this milestone.

## Deliverables

- `docs/PRODUCT.md`: concise normative V1 product boundary.
- `DOCUMENT_STATUS.md`: every repository design/research document classified.
- `BASELINE.md`: reproducible feedback-loop measurements and host information.
- `DECISIONS.md`: accepted meanings of production, self-hosted, supported, and
  compatible.
- `EVIDENCE.md`: proof for every gate.
- Updates to `README.md` and document introductions so they point to the product
  contract and do not make contradictory status claims.

## Work Sequence

### 1. Inventory Claims

1. Enumerate Markdown files under the repository, excluding generated/vendor
   content.
2. For each file record purpose, claimed status, feature profile, and conflicts
   in `DOCUMENT_STATUS.md`.
3. Use only these statuses: `normative`, `proposed`, `experimental`,
   `research`, and `historical`.
4. Identify contradictions including Source/Core versions, backend promises,
   self-hosting, package support, and compile-time features.
5. Do not resolve contradictions by deleting research. Resolve public claims by
   changing status labels and authoritative links.

Verification: every Markdown file returned by the inventory command appears
exactly once, except milestone documents and clearly generated files.

### 2. Define the Product Contract

Write `docs/PRODUCT.md` with these sections:

- purpose and top-level requirements;
- V1 supported application classes;
- supported source/Core/toolchain profiles;
- initial host and target matrix;
- required commands and artifacts;
- compatibility promises and versioned public schemas;
- performance and resource-limit principles;
- security/capability boundary;
- definition of self-hosting; and
- explicit non-goals, including the later minimal-trust bootstrap.

Every statement must be testable or explicitly identified as a non-goal. Link
details instead of copying entire specifications.

### 3. Establish Baseline Measurements

Define commands and inputs for:

- clean bootstrap test-suite latency;
- repeat test-suite latency;
- clean example build/run;
- a leaf-source edit;
- a deep dependency/interface edit; and
- two or more independent worktrees building concurrently.

Record OS, architecture, CPU, memory, Python, `uv`, C compiler, command, wall
time, peak memory where available, case count, and cache state in `BASELINE.md`.
Do not present one machine's numbers as final budgets; they are the reproducible
starting point used to approve budgets.

### 4. Reconcile Entry Documentation

Update `README.md`, `REQUIREMENTS.md` links if necessary, and the opening status
paragraphs of affected documents. Preserve historical content but ensure a
reader can determine whether it is binding before reading implementation detail.

### 5. Review Against Requirements

Create a matrix mapping every top-level requirement to at least one product
contract clause and later milestone. Any uncovered requirement blocks the gate.

## Gate Checklist

- [x] `docs/PRODUCT.md` defines one testable V1 product.
- [x] Every design/research document has one explicit status.
- [x] All identified contradictory public claims are resolved or escalated.
- [x] Baseline commands run from a clean checkout and record host details.
- [x] Every top-level requirement maps to the product contract and roadmap.
- [x] README and specification entry points agree about current and future
      profiles.
- [x] Local links and Markdown checks pass.
- [x] `EVIDENCE.md` links every artifact and command result.

## Handoff

Milestone 01 may begin only with an approved public-boundary list: portable
tests cannot be classified correctly while the public contract remains unknown.

# Milestone 00: Product Definition

Implementation instructions: [PLAN.md](PLAN.md)

## Outcome

SlopH has one measurable product definition that reconciles the experimental
profiles, current V1 work, and future design documents. Contributors can tell
whether a proposed feature moves the language toward that product or violates
its governing constraints.

## Why This Exists

The repository contains executable experiments, normative-looking V1 text, and
longer-term proposals. Without an explicit classification, an implementation
can accidentally stabilize an experiment or treat a future idea as a release
requirement.

## Included

- Define the supported V1 use cases, platforms, artifacts, and compatibility
  promises.
- Define “production,” “self-hosted,” and “proper language” for this project.
- Classify every design document as normative, proposed, experimental, or
  historical.
- Establish specification-size and AI/human-readability review criteria.
- Record baseline cold, warm, dependency-invalidating, and concurrent-build
  measurements on the supported hosts.
- Establish admission and removal rules for language and toolchain features.

## Non-Goals

- Finalizing every open language-design decision.
- Choosing features only because another production language has them.
- Treating best-case cached compilation as the primary performance result.

## Dependencies

None. [REQUIREMENTS.md](../../REQUIREMENTS.md) governs this milestone.

## Exit Gates

- One indexed document identifies the supported V1 product and its non-goals.
- Every existing specification and research document has an explicit status.
- Supported platform and compatibility claims are testable rather than
  aspirational.
- Feedback-loop baselines are reproducible from a clean checkout.
- Later milestones reference the same definitions without redefining them.

## Required Evidence

- Document-status inventory.
- Baseline build and test measurements with host details.
- Review showing that the product definition satisfies every top-level
  requirement.

## References

- [Top-level requirements](../../REQUIREMENTS.md)
- [Research synthesis](../../docs/research/RESEARCH_SYNTHESIS.md)
- [V1 profile](../../docs/language/V1.md)
- [Infrastructure](../../docs/toolchain/INFRASTRUCTURE.md)

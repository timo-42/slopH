# SlopH V1 Product Contract

Status: normative. This document defines the one supported V1 product. Other
documents describe details of this product, proposals beyond it, or research;
their standing is recorded in the
[document-status inventory](../.plan/00-product-definition/DOCUMENT_STATUS.md).
The governing goals remain [REQUIREMENTS.md](../REQUIREMENTS.md); if this
contract conflicts with them, this contract must change.

## Purpose

SlopH V1 is a small, AI-first, general-purpose language that compiles to
native executables through a canonical typed Core. The product is the complete
edit–check–test–run loop, not only the compiler: fast feedback, deterministic
diagnostics, and reproducible offline builds are part of the contract.

## Supported Application Classes

V1 supports ordinary software:

- command-line tools that read arguments, streams, files, and environment and
  return an exit result;
- long-running services using the same effect-tracked APIs; and
- libraries consumed by other SlopH projects as local workspace members.

V1 is not specialized for agent workflows, ML kernels, GUIs, or embedded
targets.

## Supported Profiles

- **Source:** the [SlopH Language v1 profile](language/V1.md) (normative).
  Source v0 remains an experiment and carries no compatibility promise.
- **Core:** the canonical typed Core serialization named by the v1 profile.
  Core v0 remains an experimental test vehicle.
- **Toolchain:** the hosted Python bootstrap (`src/sloph-python-bootstrap`)
  exposed through one `sloph` command, until the self-hosted compiler replaces
  it at roadmap milestone 10. Requires Python 3.11+ and a host C11 compiler.

## Host and Target Matrix

Supported and CI-verified: macOS ARM64 and Linux AMD64, native executables via
deterministic C11 emitted for the host toolchain. Any other host or target is
unsupported and must fail with an availability diagnostic, not undefined
behavior.

## Required Commands and Artifacts

The supported commands are `sloph check`, `sloph format`, `sloph compile`, and
`sloph run` over a `sloph.json` project, plus test execution via the
repository's documented commands. Inspection (`ast`/`core` printing) is
supported for the current profile under `sloph unstable` until the CLI is
stabilized. Artifacts are native executables and canonical Core text; both
must be deterministic for identical inputs.

## Compatibility Promises

- Behavior covered by the portable corpus under [`tests/`](../tests/) is the
  public behavior; every conforming compiler must reproduce it.
- Diagnostics carry stable machine-readable identifiers; message wording may
  change, identifiers and spans may not change silently.
- The canonical Core text format and any published JSON schemas are versioned;
  incompatible changes require a new version, not an in-place edit.
- Experimental (`unstable`) commands, Source v0, and Core v0 carry no
  compatibility promise.

## Performance and Resource-Limit Principles

- Cold, warm, incremental, dependency-invalidating, and concurrent isolated
  builds are all first-class measurements; a warm cache is never the primary
  result. Reproducible baselines live in
  [BASELINE.md](../.plan/00-product-definition/BASELINE.md).
- A clean checkout must build and test locally without a network or
  pre-populated cache.
- Every user-controlled source of unbounded work (input size, recursion,
  expansion, evaluation, output) has an explicit limit with a deterministic
  diagnostic.

## Security and Capability Boundary

Effectful host access flows through effect-tracked standard-library APIs over
the reviewed [POSIX boundary](toolchain/POSIX_BOUNDARY.md). Known deviation:
the bootstrap executes dependency `build.sh` scripts during `compile`/`run`
with ambient user authority. This is accepted, documented security debt
([idea/SECURITY.md](../idea/SECURITY.md)) and must be replaced before V1 is
declared production.

## Definition of Self-Hosting

SlopH is self-hosted when the supported compiler is written in SlopH, passes
the same portable corpus as the bootstrap, and can rebuild itself from a clean
offline checkout. Self-hosting does not claim a minimal trust root; the
C90/RV32IM reproducible bootstrap is a separate, later roadmap
([BOOTSTRAP.md](toolchain/BOOTSTRAP.md)).

## Non-Goals

- Easy manual authoring by humans beyond what readability requires.
- A package registry, network dependency resolution, LSP, managed backends,
  threads/async, or WebAssembly output in V1.
- The minimal-trust bootstrap chain as a V1 requirement.
- Best-case cached compilation as the primary performance claim.

## Accepted Definitions

The accepted meanings of *production*, *self-hosted*, *supported*, and
*compatible* are recorded in
[DECISIONS.md](../.plan/00-product-definition/DECISIONS.md).

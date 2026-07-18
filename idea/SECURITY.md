# IDEA: Secure Package Build Tasks

Status: known security debt; exploratory replacement design.

## Temporary Unsafe Behavior

The bootstrap build path currently executes an executable `build.sh` found at
the root of each dependency package before `compile` or `run`. The script runs
once per build, in dependency order, with the package as its working directory
and with the invoking user's inherited environment. Read-only commands such as
`check`, formatting, and Syntax/Core inspection do not execute scripts.

This convention lets a package use C, C++, Rust, assembly, or another external
toolchain without teaching the SlopH compiler how to build those languages. It
is expedient, not a safe or final package-build contract. A timeout, bounded
diagnostic capture, and process-group termination limit accidental failures;
they do not sandbox hostile code.

## Threats

A dependency build script has the same ambient authority as the user or CI
worker running SlopH. It can:

- read and exfiltrate source, credentials, signing keys, environment variables,
  SSH agents, package tokens, and unrelated user files;
- modify the checkout, dependency packages, caches, compiler installation,
  shell configuration, or other persistent host state;
- access the network, spawn unbounded process trees, consume disk or memory,
  evade a parent-process timeout, or leave background processes behind;
- use clocks, randomness, undeclared tools, host headers, and mutable external
  state, making outputs irreproducible and cache identities unsound;
- exploit dependency confusion or first-match package search to substitute a
  package whose apparently harmless build step executes arbitrary code;
- emit a library whose ABI, symbols, target, or behavior differs from its
  binding metadata and audit claims.

AI-generated dependency changes make implicit execution especially dangerous:
an agent must be able to see that adding a dependency grants build-time code
execution. Registry signatures establish package identity, not script safety.

## Intended Replacement

The package system should replace filename discovery with explicit build-task
metadata. A task declares:

- immutable inputs and normalized output paths in an isolated build directory;
- target tuple, toolchain identities, build and host dependencies;
- allowed environment variables, filesystem roots, processes, and network
  endpoints, with ambient access denied by default;
- resource budgets and whether the task is deterministic and cacheable;
- hashes and provenance for produced headers, shared libraries, and other
  native artifacts.

Execution should use a platform-independent capability contract backed by
appropriate host isolation, such as namespaces and seccomp on Linux and an
equivalent restricted runner on Darwin. Package source and dependency stores
remain read-only. Only declared outputs may survive the task. Offline, locked,
CI, and registry modes require explicit trust policy rather than silently
weakening isolation.

The registry should normally distribute target-specific native artifacts so a
consumer does not need to execute publisher code. Local rebuilding remains
possible, but it must be an explicit, inspectable graph operation. The package
manager verifies artifact hashes, target compatibility, exported symbols, ABI
metadata, and provenance before linking.

## Exit Criteria

Automatic ambient `build.sh` execution can be removed when the toolchain has:

- a versioned build-task schema and dependency graph;
- enforceable capability isolation and declared-output handling;
- target-aware artifact hashing, caching, and reproducibility reporting;
- user and CI trust policies with clear diagnostics and non-interactive modes;
- registry support for bundled native artifacts and verified provenance.

Until then, documentation and diagnostics must describe dependency build
scripts as arbitrary code execution, and the convention must not spread into
parsing, type checking, transforms, or read-only inspection commands.

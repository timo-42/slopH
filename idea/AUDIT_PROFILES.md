# IDEA: Audit Commands and Requirement Profiles

Status: exploratory, non-normative.

SlopH should provide a general audit command that evaluates a package or closed
application against a named, versioned profile. A profile is an ordered list of
requirements with parameters and result policy. The Power of Ten becomes one
official profile built from ordinary requirement identifiers rather than ten
special cases embedded directly in the command.

The audit produces evidence, not a vague certification badge. Each requirement
reports whether it was proven, violated, left conditional on a caller, unknown
to the current analyzer, or not applicable to the selected language and target.

## Command Surface

The proposed stable commands are:

```text
sloph audit [PATH]
    --profile NAME[@VERSION]
    [--profile NAME[@VERSION]]...
    [--target TRIPLE]
    [--scope package|application]
    [--format human|json|sarif]
    [--evidence none|summary|full]
    [--deny conditional|unknown|not-applicable]...
    [-o PATH]

sloph audit profile list
    [--format human|json]

sloph audit profile show NAME[@VERSION]
    [--resolved]
    [--format human|json]
```

`PATH` defaults to the current project. Package scope may produce conditional
results because callers, generic arguments, providers, targets, or application
profiles remain unknown. Application scope resolves the locked dependency
graph and every reachable implementation for the selected target.

Multiple profiles are combined. Every distinct requirement is evaluated once,
but the report retains the profiles that requested it and applies the strictest
compatible result policy. Conflicting parameterizations fail during profile
resolution unless one explicitly refines the other according to that
requirement's documented rules.

The command exits zero only when the selected profiles' acceptance policies
are satisfied. A proven violation or a result denied by `--deny` exits with the
ordinary requested-check-failed code. Invalid profiles and unavailable checker
implementations are toolchain failures.

## Profile Model

A profile contains:

- a stable profile name and version;
- optional parent profiles;
- an ordered list of requirement instances;
- parameters for each requirement;
- the accepted result states for each requirement;
- checker identity and minimum checker version where required;
- applicable languages, editions, targets, and build phases;
- required external evidence, if any.

A conceptual manifest representation is:

```text
(audit-profile
  (name organization::flight)
  (version 1)
  (extends
    (profile sloph::power-of-ten 1))
  (requirements
    (require memory.no-host-allocation
      (parameter after initialization)
      (accept pass))
    (require ffi.declared-effects
      (accept pass not-applicable))
    (require diagnostics.zero-warnings
      (parameter level pedantic)
      (accept pass))))
```

The concrete package metadata syntax is deferred. The canonical resolved form
is data, not executable macro code. Profile resolution must be deterministic,
bounded, usable offline, and included in lock and provenance identities.

Requirement order controls presentation and deterministic evidence output. It
does not permit an earlier failure to hide later findings unless an explicit
resource limit makes the audit incomplete, in which case the report is marked
incomplete and cannot pass.

## Requirement Identity and Checkers

Each requirement has a stable versioned identity, for example:

```text
control.no-recursion@1
control.loop-bound@1
memory.no-host-allocation@1
source.function-size@1
contract.assertion-density@1
scope.minimum-binding-scope@1
contract.checked-results@1
expansion.restricted@1
memory.pointer-restrictions@1
diagnostics.zero-warnings@1
process.static-analysis@1
```

The requirement definition specifies its parameters, result states, evidence
schema, applicability rules, and which compiler representations it inspects.
A checker is an implementation of that definition. Changing the meaning of a
requirement requires a new requirement version; improving an analyzer without
changing accepted semantics may update the checker version.

The compiler distribution provides official checkers for language, Core,
reachability, effects, and backend facts it already validates. External
analyzers may provide additional checkers through explicit toolchain packages,
but the audited library cannot satisfy its own requirement merely by declaring
that it passes. Reports identify the checker and trust source for every result.

Unknown requirement identifiers are errors. Missing external checkers produce
`unknown` only when the profile explicitly allows unavailable analysis;
otherwise profile execution fails.

## Result States

Every requirement returns exactly one primary state:

| State | Meaning |
| --- | --- |
| `pass` | The selected checker proved the requirement for the stated scope and inputs. |
| `fail` | At least one concrete counterexample or violation was found. |
| `conditional` | The package satisfies the requirement only if recorded caller or build conditions hold. |
| `unknown` | The checker cannot prove or disprove the requirement within its model or limits. |
| `not-applicable` | The requirement's construct does not exist for the selected language, phase, or target. |

`unknown` never silently means `pass`. `not-applicable` is accepted only when
the profile permits it and includes an applicability explanation. A result can
also carry `incomplete: true` when resource exhaustion prevented all evidence
from being collected; incomplete results cannot satisfy a proof requirement.

A conditional result contains machine-checkable obligations rather than only
prose:

```text
(conditional
  (requires
    (provider implements collections::StaticCollections)
    (value input.length <= 4096)
    (effects callback excludes heap_reserve)
    (call-graph callback acyclic)))
```

Application audit attempts to discharge these obligations after resolving
providers, generic arguments, callbacks, effects, and reachability. Any
remaining condition is reported at the application root.

## Built-In Power-of-Ten Profile

The provisional `sloph::power-of-ten@1` profile maps Gerard J. Holzmann's rules
for safety-critical C into explicit SlopH requirements:

| Rule | Requirement | Expected library result |
| --- | --- | --- |
| Simple control flow and no recursion | `control.no-recursion@1` plus language control-flow checks | Pass or conditional for callbacks and dynamic dispatch |
| Fixed loop bounds | `control.loop-bound@1` | Pass or conditional on collection/input bounds |
| No dynamic allocation after initialization | `memory.no-host-allocation@1` | Usually conditional until application audit |
| Bounded function size | `source.function-size@1` | Directly checkable |
| Assertion density and validity | `contract.assertion-density@1` | Partly checkable; unknown where usefulness cannot be established |
| Smallest data scope | `scope.minimum-binding-scope@1` | Conservative check with possible unknowns |
| Checked returns and parameters | `contract.checked-results@1` and parameter-contract checks | Result consumption checkable; semantic validation may remain unknown |
| Restricted preprocessor | `expansion.restricted@1` | Not applicable or mapped explicitly to SlopH expansion and generated C |
| Restricted pointer use | `memory.pointer-restrictions@1` | Checked in typed Core, lower IR, FFI, and generated C |
| Pedantic zero-warning build and static analysis | `diagnostics.zero-warnings@1` and `process.static-analysis@1` | Build result checkable; recurring process evidence external |

This profile must document every departure from the original C rule. Its output
is called a Power-of-Ten assessment, not NASA/JPL certification. Passing one
profile does not imply compliance with an institution's current coding
standard, development process, verification plan, or regulatory obligations.

## Project-Defined Profiles

Projects and organizations may compose known requirements:

```text
(audit-profile
  (name example::flight)
  (version 3)
  (extends
    (profile sloph::power-of-ten 1))
  (requirements
    (require memory.profile
      (parameter profile static-only)
      (accept pass))
    (require collections.capacity-bounded
      (accept pass))
    (require ffi.allowlist
      (parameter symbols
        crypto_verify
        platform_write)
      (accept pass))
    (require dependency.no-unknown-audits
      (accept pass))))
```

Profiles may strengthen an inherited requirement, such as reducing a function
statement limit from 60 to 40. They may not weaken a parent silently. An
explicit replacement records the removed condition and requires a new profile
identity so downstream policy cannot confuse it with the parent.

Project profiles belong in reviewed source or a locked policy package. A CLI
flag may select a profile but must not mutate or weaken its requirement list.

## Evidence and Diagnostics

Human output is concise but identifies every non-passing result:

```text
audit: parser 1.4.0
profile: sloph::power-of-ten@1
scope: package

1  pass         no recursive call-graph component
2  conditional  iterator bound requires input.length <= 4096
3  conditional  requires StaticCollections and no runtime heap effect
4  pass         largest authored function: 43 statements (limit 60)
5  fail         parse_header has no recovery assertion
6  pass
7  pass
8  not-applicable  no textual preprocessor in selected source edition
9  unknown      crypto_parse lacks a verified foreign pointer summary
10 conditional  zero warnings; recurring analyzer evidence unavailable

result: fail
1 failure, 3 conditions, 1 unknown
```

Structured output includes:

- report schema and version;
- project, dependency, profile, checker, compiler, target, and source hashes;
- resolved requirement list and parameters;
- result state, locations, call paths, counterexamples, and obligations;
- resource limits and incomplete-analysis markers;
- external evidence identity and validity interval;
- deterministic ordering independent of hash-map or worker scheduling.

SARIF output is an interoperability view over the same results. JSON remains
the complete SlopH audit schema when SARIF cannot represent conditional proof
obligations or build identities precisely.

## Package Summaries and Application Closure

Compiled package interfaces may carry independently validated audit summaries:

- reachable and exported call-graph facts;
- loop-bound expressions and required value bounds;
- inferred effects and phase restrictions;
- assertion and authored-source metrics;
- pointer/reference and FFI summaries;
- conditional obligations on generic arguments, callbacks, and providers;
- the source, Core, compiler, checker, and profile hashes supporting the
  summary.

Summaries are accelerators, not authority. A consuming compiler validates their
schema, identities, and compatibility and may recompute them. Changed private
bodies invalidate relevant summaries without necessarily invalidating ordinary
public type interfaces.

Only a closed application audit can establish requirements involving all
reachable code, selected providers, runtime helpers, target backends, and
foreign libraries. A package can honestly report `pass` for intrinsic rules
and `conditional` for application-dependent rules without being labeled
noncompliant.

## Package Claims, Attestations, and Registry Metadata

Every published library package should ship an inert, bounded
`audit-claims.sloph` file inside its canonical archive. It records the profiles
the publisher claims to have evaluated, claimed results and conditions, covered
targets, and compiler and checker identities. A package making no claim still
ships the canonical empty form so absence cannot be confused with truncated
metadata.

A conceptual self-claim is:

```text
(audit-claims 0
  (claims
    (claim
      (profile sloph::power-of-ten 1)
      (scope package)
      (targets x86_64-linux aarch64-macos)
      (report-hash sha256:...)
      (result conditional)
      (conditions
        (provider implements collections::StaticCollections)
        (effects application excludes heap_reserve)))))
```

This is untrusted package content. A package can lie, omit a violation, use a
modified checker, claim a different target, or submit stale results. Its
signature proves who supplied the claim and protects the bytes; it does not
make the claim true.

After upload, the registry or library CDN independently:

1. validates the canonical archive, manifest, dependency data, and claim;
2. resolves exact declared dependencies in a hermetic audit environment;
3. reruns supported claimed profiles and profiles required by registry policy;
4. compares the publisher claim with the independently generated report;
5. applies documented accept, reject, quarantine, or visibly-unverified policy;
6. publishes a signed audit attestation bound to the immutable package hash.

The registry must never copy a publisher result into a verified field merely
because the publisher signed it. A mismatch remains visible evidence.

Three metadata layers remain distinct:

1. **Canonical package content.** Immutable source, manifest, dependency data,
   and publisher audit claims. This content receives the package hash and is
   sufficient for source builds.
2. **Verification attestations.** Immutable signed reports produced after
   upload by the registry or independent auditors. They bind exact package,
   dependency-lock, target, compiler, checker, profile, and resource-limit
   identities.
3. **Registry metadata.** External service-owned data such as upload date,
   uploader account, moderation and yank state, download counts, mirrors,
   latest-verification pointers, presentation text, and AI-generated discovery
   metadata.

Attestations cannot live inside the archive they identify: adding the
attestation would change the package hash and require another attestation
recursively. They are content-addressed objects stored beside the package.

Registry metadata is expected to change. Upload timestamps and download counts
must not enter language semantics, artifact identities, or locked build output.
Security or yank policy may affect new dependency resolution, but an existing
lockfile continues to identify exact package bytes and reports the policy state
separately rather than silently selecting different content.

After validating an upload, the registry may generate:

- a concise plain-language summary of what the library does;
- normalized search keywords and categories;
- optional capability hints and likely use cases;
- a warning that the generated description may be incomplete or incorrect.

Generated discovery metadata records the package hash, selected source files,
model/provider identity, prompt-template version, generation time, language,
and moderation status. It is clearly labeled as AI-generated and remains
separate from publisher-authored descriptions and independently verified audit
results. Regeneration creates a new metadata revision without changing the
package or its attestations.

Package source, README text, examples, and comments are untrusted model input
and may contain prompt-injection attempts. Generation runs without registry,
network, signing-key, package-publication, or audit-authority capabilities. The
model emits a small bounded schema rather than arbitrary HTML or executable
content. Keywords are normalized, deduplicated, length-limited, and passed
through ordinary abuse and policy filters.

AI summaries and keywords may influence search and presentation. They never:

- change dependency resolution compatibility or locked package identity;
- establish that an API, security property, or audit requirement is present;
- replace source, manifest, interfaces, or verified documentation;
- inherit trust from a registry audit signature;
- become compiler inputs or package-manager semantic metadata.

Publishers may suggest corrections or request regeneration, but the API keeps
publisher-authored text and registry-generated text distinguishable. A registry
may retain previous revisions for moderation and reproducibility of search
results.

A conceptual attestation contains:

```text
(audit-attestation 0
  (issuer registry.example)
  (package sloph::parser 1.4.0 sha256:PACKAGE)
  (profile sloph::power-of-ten 1)
  (scope package)
  (target x86_64-linux)
  (compiler sha256:COMPILER)
  (checker-set sha256:CHECKERS)
  (dependency-lock sha256:LOCK)
  (limits sha256:LIMITS)
  (publisher-claim sha256:CLAIM)
  (verified-report sha256:REPORT)
  (comparison mismatch)
  (result fail)
  (signature ...))
```

Registry APIs and user interfaces expose provenance separately:

```text
publisher claim:       conditional
registry verification: fail
independent auditor:   unknown
uploaded:              2030-04-12T08:31:22Z
AI summary:            parser combinators for bounded ASCII protocols
AI keywords:           parsing, combinators, bounded-input, no-std
```

There is no undifferentiated `compliant: true`. Consumers select trusted
attestation issuers in project or organization policy. Serving an attestation
does not automatically make a CDN a trusted auditor.

Multiple auditors' attestations coexist. A newer checker or stronger limit
produces a new immutable attestation rather than overwriting history.
Supersession and revocation are separate signed records. Registry verification
is normally package-scoped and preserves application-dependent conditions; it
cannot claim that an unknown future application discharges them.

Trusted compatible attestations are audit accelerators. Source-only local
re-audit remains possible, and correct builds do not require an online CDN.
The broader external metadata, AI discovery, download-link, and GitHub asset
design is recorded in
[Library Registry Metadata and GitHub-Backed CDN](./LIBRARY_CDN.md).

## Process Requirements

Some requirements concern development activity rather than program semantics:
daily analyzer execution, review approval, tool qualification, or test history.
The compiler cannot prove these from source.

Such requirements consume external evidence records from CI or another
explicit provider. An evidence record includes issuer, subject hashes,
requirement identity, tool identity, result, timestamp, validity policy, and
signature where required. Offline builds can validate already available
evidence but do not contact an ambient service during compilation.

Missing, stale, untrusted, or mismatched process evidence is `unknown` or
`fail` according to the profile. It is never manufactured from a successful
local build.

## Reproducibility and Security

- Profile packages and external evidence are untrusted inputs and receive
  bounded decoding and schema validation.
- Audit checkers receive explicit source/Core/artifact capabilities and cannot
  perform undeclared network or filesystem access.
- Pure checker results are cached by semantic inputs, checker identity, target,
  parameters, and resource limits.
- A cached result cannot be reused for a different dependency graph, backend,
  foreign library, or profile version.
- Audit success must not depend on a remote registry, shared cache, or online
  evidence service when all locked inputs and evidence are available locally.
- Reports expose trusted assertions and unchecked foreign boundaries rather
  than hiding them behind an overall pass state.
- Publisher claims, registry attestations, independent-auditor attestations,
  and mutable registry metadata retain distinct provenance.
- Upload audits have explicit CPU, memory, output, dependency, checker, and
  wall-time limits so malicious packages cannot consume unbounded CDN
  resources.

## Implementation Stages

1. Add the profile schema, deterministic resolver, result states, and
   human/JSON reports.
2. Implement direct compiler checks for recursion, authored function size,
   checked results, warnings, and language-inapplicable constructs.
3. Add loop-bound expressions, allocation effects, phase checking, pointer/FFI
   summaries, and conditional obligations.
4. Close conditions during application reachability and provider resolution.
5. Add canonical package self-claims, external registry metadata, and signed
   registry/CDN verification attestations.
6. Add external checker packages, SARIF projection, and process-evidence
   validation.
7. Publish `sloph::power-of-ten@1` only after its SlopH mappings and limitations
   have independent review and conformance tests.

The early command remains useful even when several requirements report
`unknown`. It must never improve the displayed status merely to make a profile
look complete.

## Questions to Resolve

- The exact canonical profile and evidence schemas.
- Whether requirement versions use integers or semantic versions.
- Which checker improvements can preserve a requirement version.
- How profiles express valid refinement versus incompatible replacement.
- Default acceptance policy for `conditional`, `unknown`, and
  `not-applicable`.
- Whether project-defined profiles can name external requirement definitions in
  v1 or only compose compiler-known requirements.
- How deliberate nonterminating event loops are represented under a bounded
  loop profile.
- How authored and macro-expanded function-size limits interact.
- The initial assertion and parameter-contract language required for meaningful
  Power-of-Ten checks.
- Tool qualification and evidence-signing expectations for safety-oriented
  organizations.
- Whether the complete publisher report is embedded or only a canonical
  summary and report hash.
- Registry policy for false claims, unsupported profiles, audit timeouts, and
  checker infrastructure failures.
- Attestation discovery, expiry, supersession, and revocation schemas.
- Which mutable registry fields may influence new resolution policy without
  affecting locked build identity.
- The bounded AI summary/keyword schema, source selection, supported languages,
  correction workflow, and retention policy.
- Whether search ranking exposes the AI metadata revision used for a result.

## References

- [The Power of Ten: Rules for Developing Safety-Critical Code][power-of-ten]
- [Policy-Selected Collection Interfaces](./libraries/collections.md)
- [Inferred Effects and Explicit Public Contracts](./EFFECTS.md)
- [Reachability-Driven Compilation](./REACHABILITY_COMPILATION.md)
- [Package Metadata and Ordered Search Paths](./PACKAGE_SEARCH_PATH.md)

[power-of-ten]: https://spinroot.com/gerard/pdf/P10.pdf

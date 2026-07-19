# Milestone 00 Decisions

Accepted meanings used by the product contract and later milestones.

## Production

A toolchain build is *production* when: it implements the normative V1
profile with no undocumented host-language shortcuts; every public behavior is
covered by the portable corpus; diagnostics are deterministic with stable
identifiers; documented resource limits bound all user-controlled work; the
feedback-loop budgets accepted from the baselines are met; and dependencies
are restricted to declared inert metadata and reviewed static sources.
"Production" describes evidence, not
intent.

## Self-Hosted

The supported compiler is written in SlopH, passes the same portable corpus as
the authoritative hosted compiler on the supported host matrix, and rebuilds itself from a
clean checkout without a network or pre-populated cache. Self-hosting does not
include the minimal-trust C90/RV32IM bootstrap, which is a separate roadmap.

## Supported

A platform, command, or behavior is *supported* only when it is named in
`docs/PRODUCT.md`, exercised by CI on the supported host matrix, and covered
by portable tests. Everything else is experimental or proposed and may change
without notice; unsupported targets fail with availability diagnostics.

## Compatible

A change is *compatible* when every case in the portable corpus still passes,
diagnostic identifiers and spans are preserved, and versioned public schemas
(canonical Core text, JSON schemas, manifests) parse under their declared
version. Incompatible changes require a new schema or profile version.

## Rejected Alternatives

- Treating "stable" wording in entry documents as a compatibility promise;
  compatibility is defined only by the product contract and corpus.
- Making the minimal-trust bootstrap part of the V1 definition of done.
- Defining "supported" by implementation existence rather than CI and corpus
  evidence.

# IDEA: Library Registry Metadata and GitHub-Backed CDN

Status: exploratory, non-normative.

SlopH can begin with a lightweight library registry whose package bytes are
hosted by GitHub Releases or similar ordinary HTTPS object hosts. The registry
owns metadata, indexing, audit verification, AI-generated discovery text, and
integrity records; the byte host acts only as a CDN.

Package-authored metadata and registry-generated metadata remain separate. The
registry AI should read both the safely unpacked package and publisher-provided
presentation metadata, then generate its own summary and keywords in an
external record. It never rewrites or pollutes the uploaded archive.

## Metadata Layers

The system has four explicit layers:

1. **Semantic package metadata.** `package.sloph` is inert canonical metadata
   used for compilation and dependency resolution.
2. **Publisher presentation and claims.** `publish.sloph` and
   `audit-claims.sloph` travel inside the canonical package. They may contain
   descriptions, keywords, upstream links, and audit claims. They are
   untrusted statements by the publisher.
3. **Registry version metadata.** An external signed record binds the package
   hash to upload facts, independently verified download locations, audit
   attestations, and separately namespaced AI-generated discovery metadata.
4. **Operational registry data.** Mutable download counters, search ranking,
   moderation queues, and availability observations are service state. They do
   not affect package or artifact identity.

Only the first layer determines program meaning. The second is immutable
because it is inside the package hash, but it is not trusted. The third is
independently attributable registry evidence. The fourth may change frequently.

## Publisher Metadata

Every package should ship a bounded inert `publish.sloph` file. An empty file
in canonical form is valid when a publisher provides no presentation data.

Conceptually:

```text
(publish 0
  (summary "Parser combinators for bounded ASCII protocols.")
  (keywords parsing combinators protocol ascii bounded)
  (homepage "https://example.org/parser")
  (repository "https://github.com/example/sloph-parser")
  (license-expression "Apache-2.0")
  (download-suggestions
    (download
      (kind source)
      (url "https://github.com/example/sloph-parser/releases/download/v1.4.0/parser-1.4.0.tar.zst"))))
```

Publisher summaries, keywords, and URLs can be inaccurate or malicious. They
are displayed with publisher provenance and provided to the registry's AI as
untrusted context. They never become verified merely because the archive or
publisher signature is valid.

The package archive also contains the separate `audit-claims.sloph` described
in [Audit Commands and Requirement Profiles](./AUDIT_PROFILES.md). Audit claims
need independent validation and should not be mixed with descriptive keywords.

## AI-Generated Summary and Keywords

After upload validation, a registry worker receives:

- the safely unpacked canonical package content;
- semantic `package.sloph` metadata;
- publisher `publish.sloph` metadata;
- exported interfaces or validated public Syntax/Core when available;
- README, documentation, examples, and license files within explicit limits;
- independently verified audit facts, labeled as evidence rather than prose.

The worker produces a bounded schema such as:

```text
(ai-presentation 0
  (package-hash sha256:PACKAGE)
  (generator
    (provider registry-ai)
    (model MODEL_ID)
    (prompt-template 3)
    (generated-at "2030-04-12T08:35:09Z"))
  (summary "Builds bounded ASCII protocol parsers from composable primitives.")
  (keywords parsing parser-combinators ascii bounded-input protocol)
  (categories data parsing)
  (confidence informational)
  (moderation accepted))
```

The AI output is stored only in external registry metadata. It does not modify
`publish.sloph`, package source, package hashes, audit claims, or audit
attestations. The UI and API expose both descriptions:

```text
publisher summary: ...
AI-generated summary: ...
```

The generator treats all package text as hostile data. A README may contain
prompt injection, false claims, huge generated files, encoded payloads, or
instructions to contact external systems. The worker therefore:

- has no registry mutation, signing-key, network, publication, or audit
  authority;
- reads only safely extracted, size-limited, declared file types;
- never executes package code, macros, build tasks, examples, or native files;
- uses clear data delimiters and a fixed system-owned prompt template;
- emits only a length-limited schema, not arbitrary HTML or executable text;
- normalizes and deduplicates keywords;
- sends results through ordinary abuse, malware-description, and moderation
  filters before publication.

Generated metadata records exact inputs and generator identity so it can be
reproduced approximately, audited, superseded, or regenerated. Model output is
not assumed bit-reproducible. A new generation creates a new registry-metadata
revision rather than silently rewriting historical provenance.

AI summaries and keywords are discovery aids. They do not establish API,
security, compatibility, performance, licensing, or compliance facts. Search
may use them, but search results should identify the metadata revision used
when reproducibility matters.

Publishers may submit corrections or request regeneration. Publisher-authored
replacement text remains in the publisher namespace; it never masquerades as
the registry AI's independent output, and the registry AI never masquerades as
the publisher.

## External Version Metadata

The registry produces one signed, versioned external record for an immutable
package version. Conceptually:

```text
(registry-version 0
  (package sloph::parser 1.4.0)
  (package-hash sha256:PACKAGE)
  (uploaded-at "2030-04-12T08:31:22Z")
  (uploader publisher-key:...)
  (state active)
  (publisher-presentation-hash sha256:PUBLISH)
  (ai-presentation sha256:AI_PRESENTATION)
  (audit-attestations
    sha256:ATTESTATION_1
    sha256:ATTESTATION_2)
  (downloads
    (download
      (kind source)
      (url "https://github.com/sloph-packages/packages/releases/download/parser-1.4.0/parser-1.4.0.tar.zst")
      (media-type "application/zstd")
      (size 18422)
      (hash sha256:PACKAGE)
      (priority 10))
    (download
      (kind source)
      (url "https://mirror.example/sloph/parser/1.4.0.tar.zst")
      (media-type "application/zstd")
      (size 18422)
      (hash sha256:PACKAGE)
      (priority 20)))
  (signature ...))
```

The exact schema and compression format remain deferred. Download entries must
include:

- artifact kind: canonical source, interface, Core, native object, audit
  report, Wasm module, JavaScript binding or adapter, TypeScript declaration,
  source map, or another defined kind;
- HTTPS URL;
- exact content hash and byte size;
- media type and encoding;
- target, ABI, compiler, Core, and feature identities when applicable;
- backend and host-profile identities when applicable;
- deterministic priority or mirror role;
- optional independent signature identity.

A URL is a locator, never identity or integrity evidence. Hosts may delete,
replace, redirect, rate-limit, or serve incorrect bytes. The client validates
size before unbounded buffering, streams through the required content hash,
validates the artifact schema, and rejects mismatches. It may then try another
declared mirror according to deterministic policy.

Publisher download suggestions are not automatically copied into the verified
download list. The registry fetches and validates them or uploads the exact
bytes to a registry-controlled host before publishing a download entry.

## GitHub Releases as the Initial CDN

The first registry can use GitHub Release assets to host:

- canonical compressed package archives;
- optional compiled interfaces, Core, and native accelerators;
- optional Wasm modules, JavaScript host bindings, TypeScript declarations,
  and source maps;
- audit reports and attestations;
- signed registry metadata snapshots or indexes.

This avoids building an object-storage service before package volume requires
one. GitHub provides availability and bandwidth, not SlopH package trust. The
SlopH client trusts configured registry signing keys and content hashes, not a
release tag, repository name, asset filename, TLS connection, or GitHub account
alone.

GitHub assets can be removed or replaced by an authorized account. Every asset
therefore retains an external content identity and may have multiple mirrors.
Moving bytes to S3, an OCI registry, another forge, or a dedicated CDN later
only updates download-locator metadata; package identities and lockfiles remain
unchanged.

An initial deployment may keep signed external metadata in a reviewed Git
repository and publish package bytes as release assets. The metadata repository
provides history and review, while a generated content-addressed index provides
efficient lookup. Directory or Git commit identity alone is not the package
identity.

## Upload Workflow

An upload proceeds conceptually as follows:

1. Receive canonical package bytes and publisher signature.
2. Validate archive paths, sizes, manifest, `publish.sloph`, audit claims,
   dependency declarations, and source identity without executing package code.
3. Compute the immutable package hash.
4. Store exact bytes in a staging area.
5. Run independent audit profiles under declared resource limits.
6. Generate separate AI summary and keywords from bounded package content and
   publisher metadata.
7. Copy or verify package bytes at one or more download hosts, initially GitHub
   Release assets.
8. Fetch each candidate URL back and verify size and hash.
9. Produce and sign the external registry-version record and audit
   attestations.
10. Atomically publish the index entry only after every required object is
    addressable.

Failures before publication leave no visible partial version. Retry reuses the
same content identities. AI-generation failure need not reject a package unless
registry policy requires generated presentation metadata; it can publish an
explicit `unavailable` state rather than fabricate a summary.

## Client Behavior

The package client resolves identity and version through signed registry
metadata, chooses a compatible artifact, and downloads it from a declared URL.
It verifies every byte against the expected identity before exposing the object
to parsers, compilers, linkers, or local caches.

The client can display:

```text
sloph package info sloph::parser --presentation all --audits --downloads
```

Offline and vendored builds use locally available canonical package bytes and
do not need mutable presentation metadata, AI output, counters, or a reachable
download URL. Deleting every registry-generated description must not change a
build.

## Mutable Operational Data

Download counts should not require resigning immutable version metadata on
every request. They live in a separate operational record or periodically
signed snapshot. Counts are approximate presentation data and may be delayed,
deduplicated, filtered, or unavailable.

Moderation and yank status can affect whether new resolution selects a version.
It does not rewrite the package archive. Locked builds identify the same bytes
and receive a policy diagnostic if the version is yanked, quarantined, or
revoked. Security policy may refuse use, but refusal is explicit rather than a
silent substitution.

## Questions to Resolve

- Exact `publish.sloph`, AI-presentation, registry-version, index, and
  operational-metadata schemas.
- Whether `publish.sloph` is mandatory-empty or optional with absence having a
  single canonical interpretation.
- Which package files are supplied to the AI and their byte/token budgets.
- AI correction, moderation, localization, revision, and retention policy.
- Registry signing, key rotation, threshold signing, and mirror trust.
- GitHub repository and release layout, rate limits, maximum asset sizes, and
  automated upload credentials.
- Whether the initial metadata repository is one global repository or sharded
  deterministically by package identity.
- Redirect, mirror retry, timeout, and maximum-download policies.
- How publisher-owned GitHub assets become verified mirrors without granting
  publishers registry metadata authority.
- When download statistics influence search ranking and how that influence is
  exposed.

## Related Ideas

- [Audit Commands and Requirement Profiles](./AUDIT_PROFILES.md)
- [Package Metadata and Ordered Search Paths](./PACKAGE_SEARCH_PATH.md)
- [Compiler and Package Infrastructure](../docs/toolchain/INFRASTRUCTURE.md)
- [Single-Binary Command-Line Interface](../docs/toolchain/CLI.md)
- [Native and WebAssembly Backends with Inferred Compatibility](./BACKENDS.md)

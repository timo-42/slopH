# IDEA: Package Metadata and Ordered Search Paths

Status: exploratory, non-normative.

SlopH should use the ordinary package system for compiler-internal, standard,
and toolchain libraries shipped with the compiler. These libraries should not
become magic imports merely because the compiler distribution supplies them.

Every package should contain a small semantic metadata file named
`package.sloph`. The compiler searches for packages in an ordered list of
package-root directories. The first root containing a compatible package wins.

## Inert Metadata Format

`package.sloph` should use a versioned, tagged S-expression format. It is inert
data, not a SlopH program. The format has no evaluation, function calls,
variables, macros, imports, interpolation, user-defined tags, or filesystem
access.

The processing pipeline is strictly:

```text
bytes -> generic S-expression tree -> validated PackageMetadata
```

The generic S-expression reader may share implementation with the Core reader,
but package metadata has its own strict decoder and grammar. The CLI must ship
this small bounded parser and must not require a third-party parser library.
Package discovery therefore remains available during bootstrap and before any
package source, macro, or build task can execute.

A conceptual package manifest is:

```text
(package 0
  (name sloph::collections)
  (version 1.4.2)
  (edition 2028)
  (core 1)
  (kind library)

  (sources
    (dir src))

  (dependencies
    (dependency sloph::core (exact 1.0.0))
    (dependency sloph::text (compatible 2.1.0)))

  (exports
    (module collections)
    (module collections::map)))
```

The exact field catalog and atom grammar remain design work. The semantic
metadata must be able to describe at least:

- package identity and version;
- language edition and Core compatibility;
- package kind;
- source roots and exported modules;
- normal, development, build, and compile-time dependencies;
- features and target requirements;
- declared build inputs and capabilities where applicable.

Descriptions, README content, publisher profiles, download counts, and other
registry presentation data do not affect builds and should not enlarge the
compiler manifest format. A registry may store them separately.

The parser rejects unknown tags, duplicate singleton fields, incorrect arity,
unsupported format versions, malformed atoms, and input exceeding explicit
byte, token, nesting, node, or token-size limits. Diagnostics contain stable
spans and codes. Decoding must not ignore an unknown semantic field in the hope
that an older compiler can still build the package correctly.

Package paths in metadata are relative, portable paths. Absolute paths, parent
traversal, NUL, and platform-dependent path interpretations are invalid. The
exact portable character set and normalization rules remain to be specified.

## Package-Root Layout

A root should make a requested package identity directly addressable without a
recursive filesystem scan. One possible layout is:

```text
ROOT/sloph/collections/1.4.2/package.sloph
ROOT/sloph/text/2.1.0/package.sloph
```

The identity-to-directory encoding and version ordering rules remain deferred
until package identity and version syntax are selected. Whatever encoding is
chosen must be reversible, deterministic, collision-free on supported
filesystems, and independent of directory enumeration order.

Each discovered package is an ordinary package containing canonical source and
its complete manifest. Compiled interfaces, typed Core, and native objects are
optional validated accelerators under the existing artifact rules; they do not
replace the semantic package metadata or source fallback.

## Effective Search Order

The effective package-root list is ordered as follows:

1. package paths supplied explicitly on the CLI, in argument order;
2. paths declared by the project or workspace manifest, in declaration order;
3. the compiler installation's bundled-package root.

Manifest paths are resolved relative to the manifest that declares them. CLI
paths provide temporary development overrides. Ambient environment variables
do not silently add roots because doing so would make builds depend on
undeclared machine state.

The compiler must expose the complete effective list through build inspection
and diagnostics. Reordering roots is a semantic package-resolution change, not
merely a performance preference.

## First Compatible Root Wins

For an unlocked dependency request, the resolver visits roots in order. The
first root containing at least one version that satisfies the requested package
identity, version constraint, language edition, Core revision, target, and
required features owns that request. Deterministic version selection occurs
only among compatible candidates within that root; candidates in later roots
are not considered.

Finding only incompatible versions in a root does not prevent searching the
next root. In contrast, malformed metadata for a candidate that claims the
requested identity, duplicate instances of one identity and version within a
root, or filesystem ambiguity is a hard error. The resolver must not hide a
broken or malicious shadowing package by silently falling through.

Resolution output should report:

- the dependency request;
- the selected identity, version, and content hash;
- the package root from which it was selected;
- the reason earlier roots did not supply it;
- compatible candidates shadowed in later roots.

This information must also be available in a deterministic machine-readable
form for AI tooling.

## Lockfiles and Reproducibility

A lockfile records the selected package identity, exact version, content hash,
dependency edges, enabled features, and compatibility-relevant metadata. It
does not record an absolute local directory as the package's semantic identity.

During a locked build, the search path locates the exact locked package. A
candidate with the locked identity and version but a different content hash is
a hard integrity or configuration error. The resolver does not continue to a
later root looking for bytes that match, because doing so would make a
shadowing mismatch easy to overlook.

CLI development overrides remain compatible with reproducible builds only
when their selected content matches the lockfile. Updating an override requires
an explicit resolution or lockfile update operation. Registry downloads and
vendoring populate package roots; they do not introduce separate resolution
semantics.

## Compiler-Shipped Packages

The compiler distribution supplies a final bundled-package root containing the
mandatory library, official standard packages, and any public toolchain
libraries shipped for offline use. Except for the compiler-coupled `core`
package, these are normal packages. A project or CLI root may provide a
compatible replacement earlier in the search order.

The mandatory `core` package is resolved outside ordinary shadowing. The
compiler loads only the `core` identity and content hash it was built to use,
from its installation. An earlier root cannot replace it. Compiler-development
experiments that change `core` require a deliberately different compiler build
or a future explicitly unsafe internal command, not normal project resolution.

Shipping packages with the compiler does not make them implicit dependencies.
A project manifest or another package must still declare every dependency
required by its modules. Unused bundled packages are not parsed, checked,
compiled, or linked.

## Questions to Resolve

- The exact manifest grammar, atom grammar, and canonical printer.
- Package identity, version, and version-constraint syntax.
- The collision-free identity-to-directory encoding.
- Whether package roots require a signed or content-addressed index as an
  optional lookup accelerator.
- How workspace-local packages participate in the ordered root list.
- The CLI spelling for adding, inspecting, and temporarily overriding package
  paths.
- Whether a future global user configuration may add roots when explicitly
  enabled by the project.

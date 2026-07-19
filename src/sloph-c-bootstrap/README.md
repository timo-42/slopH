# SlopH C11 bootstrap

This directory contains the authoritative hosted C11 compiler.
The public library interface is in `include/sloph`; implementation-only bounded
buffers and arenas live in `src/internal.h`. Library code reports
`SlophStatus` and stores structured diagnostics in an opaque `SlophContext`; it
does not exit the process or write to ambient streams.

Build the compiler and static library offline, then run the C unit tests:

```text
make -C src/sloph-c-bootstrap
make -C src/sloph-c-bootstrap test
make -C src/sloph-c-bootstrap cases
make -C src/sloph-c-bootstrap sanitize
```

The dependency-aware build honors `CC`, `AR`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS`,
and `LDLIBS`, enables strict C11 warnings as errors, and writes only to the
ignored `src/sloph-c-bootstrap/build` directory. The compiler is written to
`build/bin/sloph`. The `cases` target runs the Core, Source, native, and bundled
library suites. The `sanitize` target runs the C unit and CLI suite under
AddressSanitizer and UndefinedBehaviorSanitizer.

The explicit compiler-stage commands are `canopy-to-crown`,
`crown-to-heartwood`, and `heartwood-to-timber`. Native compilation passes
reviewed provider `.c` and `.S` sources directly to the host C compiler; it
does not execute package build scripts or load prebuilt shared providers.

## Hosting and allocation

`SlophContextConfig` accepts a complete fallible allocator and positive limits.
The default limits are the repository's authoritative hosted limits.
`SlophHost` makes filesystem, target, and monotonic-time effects explicit;
`sloph_posix_host()` supplies the hosted implementation. Buffers, arenas,
input reads, diagnostic storage, and atomic writes all enforce caller-selected
byte bounds.

## Vendored dependency

yyjson 0.12.0 is pinned and stored unchanged in `vendor/yyjson`. See its
`PROVENANCE.md` for the upstream commit, license, and checksums. Builds never
access the network.

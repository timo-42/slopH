# Bundled core libraries

Status: implemented Source v1 library profile, July 2026.

The small intrinsic `core` surface provides unbounded `Int`, immutable
`Bytes`, owned `Block`, checked byte indexing/slicing/concatenation and UTF-8
validation, and bounded block allocation/access/copy/freeze/release. Higher
level facilities remain ordinary layered packages.

## Packages

- `collections` provides immutable `List`, `Queue`, `Map`, and `Set`. Lists
  also provide the stack operations through prepend/head/tail.
- `std::io` provides typed standard input, standard output, and standard error,
  complete writes, bounded whole-input reads, `Result` APIs, and trapping
  convenience wrappers.
- `filesystem` provides whole-file read/write/copy, five explicit write modes,
  metadata and file kinds, existence checks, synchronization, directory
  creation/removal, file removal, and rename. File descriptors are private.
- `json` parses and deterministically encodes null, booleans, unbounded
  integers, UTF-8 strings, arrays, and ordered objects. Floating-point JSON
  numbers are rejected. Escapes and surrogate pairs are decoded, trailing data
  is rejected, and duplicate object names are errors.
- `logging` writes synchronous structured JSON lines. The caller supplies the
  timestamp; each event has a level, UTF-8 message, and JSON-valued fields.
  Logger filtering, standard-error defaults, recoverable errors, and a trapping
  wrapper are provided.
- `ctypes` provides checked fixed-width C integer values plus explicitly unsafe
  integer-shaped addresses and length-encoded byte-array descriptions branded
  with access capabilities. Actual pointer access remains restricted to
  compiler-reviewed foreign adapters.

Whole-input and whole-file reads currently reject inputs larger than 16 MiB.
Applications that need unbounded or incremental processing should use a future
streaming file API rather than relying on a single allocation.

All bundled packages have independently compiled native tests under
`src/libraries/*/*/tests`. Run them through `make cases` or the entire repository
gate through `make check`.

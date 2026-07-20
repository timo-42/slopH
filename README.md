# SlopH

SlopH is the working name for a small, AI-first, general-purpose programming
language that compiles to native executables. The authoritative compiler in
this repository is the hosted C11 implementation in `src/sloph-c-bootstrap`.
It implements the supported Source v1 path as well as the versioned Source v0
and Core v0 compatibility experiments.

In project terminology, **SlopH** is the user-facing language and **SlopH
Core** (or **Core**) is its canonical typed intermediate language. The named
compiler representations are:

```text
SlopH source -> Canopy -> Crown -> Heartwood -> Timber -> native executable
```

- **Canopy** is parsed source syntax.
- **Crown** is deterministic transformed/desugared AST JSON.
- **Heartwood** is validated, elaborated typed Core.
- **Timber** is deterministic portable C11 passed to the host C compiler.

See the [pipeline and T-diagrams](docs/toolchain/PIPELINE.md) for the stage
contracts.

The design prioritizes code that AI systems can generate and reason about,
human reviewability, a specification small enough to fit in an AI context
window, and fast compilation. Easy manual authoring by humans is not a primary
goal.

## Build and test

A C11 compiler, `make`, `ar`, and the platform linker are required. The build
is offline: yyjson is pinned under `src/sloph-c-bootstrap/vendor/yyjson`.

```text
make
make test
make cases
make check
make sanitize
```

The compiler is written to `src/sloph-c-bootstrap/build/bin/sloph`. The
`cases` target runs the Core, Source, native, and bundled-library suites.

Run the checked-in example with:

```text
src/sloph-c-bootstrap/build/bin/sloph run examples/hello-world
```

The supported public commands are:

```text
sloph check
sloph format
sloph ast print
sloph ast check
sloph core print
sloph core check
sloph compile
sloph run
```

The explicit stage commands are:

```text
sloph canopy-to-crown
sloph crown-to-heartwood
sloph heartwood-to-timber
```

Use `sloph COMMAND --help` for exact arguments.

## Projects and native providers

Projects use strict `sloph.json` format 1 manifests. Unknown or duplicate
keys, malformed JSON, wrong value shapes, unsupported formats, and source-root
escapes are rejected. Bundled dependencies are named explicitly by the
manifest.

Native providers use strict `provider.json` format 1 metadata plus versioned
`bindings.json`. A provider declares reviewed local `.c` and `.S` source files;
the compiler passes those files directly to the host C compiler. Dependency
scripts are never executed, prebuilt shared providers are not loaded, and no
runtime rpath is installed.

## Start here

1. Read the governing [requirements](REQUIREMENTS.md).
2. Read the normative [V1 product contract](docs/PRODUCT.md) and
   [language profile](docs/language/V1.md).
3. Read the [Core design](docs/language/CORE.md) and
   [transformation design](docs/language/MACRO.md).
4. Read the [pipeline and T-diagrams](docs/toolchain/PIPELINE.md),
   [CLI contract](docs/toolchain/CLI.md), and
   [testing guide](docs/toolchain/TESTING.md).
5. Read the [infrastructure requirements](docs/toolchain/INFRASTRUCTURE.md) and
   future [minimal-trust bootstrap plan](docs/toolchain/BOOTSTRAP.md).
6. Read the [bundled core-library profile](docs/libraries/CORE_LIBRARIES.md).

## Repository layout

```text
.
├── REQUIREMENTS.md
├── docs/
│   ├── language/             language and Core design
│   ├── toolchain/            compiler, CLI, testing, and bootstrap design
│   └── research/             clearly non-normative research
├── src/
│   ├── libraries/            bundled SlopH libraries and native boundaries
│   └── sloph-c-bootstrap/    authoritative hosted C11 compiler
├── examples/                 CI-verified Source v1 projects
├── tests/                    portable cases and implementation adapters
└── .github/workflows/        Linux and macOS CI
```

## Further reading

- [Source v1](docs/language/V1.md)
- [Core](docs/language/CORE.md)
- [Canopy → Crown → Heartwood → Timber](docs/toolchain/PIPELINE.md)
- [POSIX/native boundary](docs/toolchain/POSIX_BOUNDARY.md)
- [Research synthesis](docs/research/RESEARCH_SYNTHESIS.md)

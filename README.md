# SlopH

SlopH is the working name for a small, AI-first, general-purpose programming
language intended to compile to native executables. The repository now contains
experimental Core v0 tools and a deliberately small Source v0 to native C11
vertical slice. These are executable design experiments, not yet the final
source language, runtime, or native backend.

In project terminology, **SlopH** is the user-facing language and **SlopH
Core** (or **Core**) is its canonical typed intermediate language. “Source v0”
and “Source v1” name versioned implementation profiles rather than separate
languages. The lowercase `sloph` package owns ordinary language-level data,
the lowercase `core` package exposes fixed Core types and primitives, and the
mandatory `prelude` re-exports their fundamental types. See the normative
[v1 terminology](docs/language/V1.md#terminology).

The design prioritizes code that AI systems can generate and reason about,
human reviewability, a specification small enough to fit in an AI context
window, and fast compilation. Easy manual authoring by humans is explicitly not
a primary goal.

## Start Here

1. Read the governing [top-level requirements](REQUIREMENTS.md).
2. Read the [research synthesis](docs/research/RESEARCH_SYNTHESIS.md) for the
   rationale behind the current direction.
3. Read the [Core design](docs/language/CORE.md) and
   [transformation design](docs/language/MACRO.md).
4. Read the [experimental Core v0 profile](docs/language/CORE_V0.md) to
   understand the executable subset currently being tested.
5. Read the [infrastructure requirements](docs/toolchain/INFRASTRUCTURE.md) and
   [bootstrap plan](docs/toolchain/BOOTSTRAP.md).

## Try SlopH

The stable v1 vertical slice can check, format, inspect, compile, and run Source
projects. The [examples](examples/README.md) are executable documentation and
are verified on every supported CI platform. After installing the checkout,
run Hello World with:

```text
sloph run examples/hello-world
```

## Experimental Tools

Core v0 is a monomorphic, pure subset used to test the typed Core
representation before the full source language is fixed. The Python 3.11+
implementation also supports explicit Source v1 generics in canonical Core v2,
with generic `Option` and `Result` in `sloph` and recoverable
`std::io::try_write`. It has no third-party runtime dependencies and exposes:

```text
sloph unstable core check
sloph unstable core print
sloph unstable core eval
sloph unstable check
sloph unstable format
sloph unstable ast print
sloph unstable ast check
sloph unstable compile
sloph unstable run
```

Core commands accept the canonical tagged S-expression text format. The source
profile adds multi-module parsing, formatting, AST JSON, Core lowering, and a
first-order C11 native bridge. Neither profile provides Core JSON/binary,
optimization, effects, higher-order native functions, or a final application
ABI. See the [Core v0 specification](docs/language/CORE_V0.md),
[experimental CLI profile](docs/toolchain/CLI.md#experimental-source-to-native-v0-tools),
and [v0 test profile](docs/toolchain/TESTING.md#experimental-v0-test-profile).
The new vertical slice is specified by
[Source v0](docs/language/SOURCE_V0.md) and the
[experimental C11 backend](docs/toolchain/C_BACKEND_V0.md).

Python 3.11 or newer and `uv` are required. Run the hosted Python bootstrap and
its complete test suite directly from a checkout without installing a project:

```text
uv run --no-project --directory src/sloph-python-bootstrap python -m sloph --help
uv run --no-project --directory src/sloph-python-bootstrap \
  python -m unittest discover -s tests -t .
uv run --no-project --directory src/sloph-python-bootstrap \
  ../libraries/run-tests.sh python -m sloph
```

Bootstrap `compile` and `run` commands currently execute package-root
`build.sh` files belonging to dependencies. Treat adding such a dependency as
granting arbitrary build-time code execution; the intended sandboxed
replacement is tracked in [`idea/SECURITY.md`](idea/SECURITY.md).

## Repository Layout

~~~text
.
├── REQUIREMENTS.md       Governing goals and non-goals
├── docs/
│   ├── language/         Language and Core design
│   ├── toolchain/        Compiler, CLI, testing, and bootstrap design
│   └── research/         Academic and practitioner research
├── src/
│   ├── libraries/        Bundled SlopH libraries and native boundaries
│   └── sloph-python-bootstrap/
│       ├── sloph/        Hosted Python toolchain libraries and thin CLI
│       ├── tests/        Python-bootstrap tests and portable-corpus adapter
│       └── pyproject.toml
├── examples/             CI-verified stable v1 projects
├── tests/                Portable Core/Source cases and golden files
├── .github/workflows/    Linux and macOS CI
└── LICENSE
~~~

### Language design

- [Small typed Core](docs/language/CORE.md)
- [Experimental Core v0 profile](docs/language/CORE_V0.md)
- [Experimental Source v0 profile](docs/language/SOURCE_V0.md)
- [Enums, structs, Unit, and Bool](docs/language/ENUM.md)
- [Integers and bounded integers](docs/language/INTEGER.md)
- [Floating point and bounded floats](docs/language/FLOAT.md)
- [Memory management](docs/language/MEMORY.md)
- [Macros, sugar, and transformations](docs/language/MACRO.md)

### Toolchain design

- [Compiler and package infrastructure](docs/toolchain/INFRASTRUCTURE.md)
- [Single-binary CLI](docs/toolchain/CLI.md)
- [Shared testing architecture](docs/toolchain/TESTING.md)
- [Reproducible bootstrap](docs/toolchain/BOOTSTRAP.md)
- [Experimental C11 backend](docs/toolchain/C_BACKEND_V0.md)

### Research

- [Research synthesis](docs/research/RESEARCH_SYNTHESIS.md)
- [Haskell and GHC Core](docs/research/RESEARCH_GHC_CORE.md)
- [Extensible programming languages](docs/research/RESEARCH_EXTENSIBILITY.md)
- [Lisp and Forth](docs/research/RESEARCH_LISP_FORTH.md)

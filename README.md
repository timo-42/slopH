# SlopH

SlopH is the working name for a small, AI-first, general-purpose programming
language intended to compile to native executables. The project is currently in
the language-design and bootstrap-planning stage. Its first implementation
milestone is a deliberately limited Python tool for experimenting with Core v0;
it is not yet a source-language compiler or native backend.

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

## Experimental Core v0 Tools

Core v0 is a monomorphic, pure subset used to test the typed Core
representation before the full source language is fixed. The Python 3.11+
implementation has no third-party runtime dependencies and exposes only:

```text
sloph unstable core check
sloph unstable core print
sloph unstable core eval
```

The milestone accepts only the canonical tagged S-expression text format. It
does not accept or emit JSON or binary Core, compare Core documents, compile
source programs, optimize Core, or produce native, C, WebAssembly, or other
backend output. See the [Core v0 specification](docs/language/CORE_V0.md),
[experimental CLI profile](docs/toolchain/CLI.md#experimental-core-v0-tools),
and [Core v0 test profile](docs/toolchain/TESTING.md#experimental-core-v0-test-profile).

Python 3.11 or newer is required. Install the experimental CLI from a checkout
and run its complete runtime test suite with:

```text
python -m pip install --no-deps -e .
python -m unittest discover -s tests -t .
```

## Repository Layout

~~~text
.
├── REQUIREMENTS.md       Governing goals and non-goals
├── docs/
│   ├── language/         Language and Core design
│   ├── toolchain/        Compiler, CLI, testing, and bootstrap design
│   └── research/         Academic and practitioner research
├── src/sloph/            Python Core v0 libraries and thin CLI
├── tests/                Shared corpus, runner, and implementation tests
├── .github/workflows/    Linux and macOS CI
├── pyproject.toml        Python package and console entry point
└── LICENSE
~~~

### Language design

- [Small typed Core](docs/language/CORE.md)
- [Experimental Core v0 profile](docs/language/CORE_V0.md)
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

### Research

- [Research synthesis](docs/research/RESEARCH_SYNTHESIS.md)
- [Haskell and GHC Core](docs/research/RESEARCH_GHC_CORE.md)
- [Extensible programming languages](docs/research/RESEARCH_EXTENSIBILITY.md)
- [Lisp and Forth](docs/research/RESEARCH_LISP_FORTH.md)

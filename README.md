# SlopH

SlopH is the working name for a small, AI-first, general-purpose programming
language that compiles to native executables. The project is currently in the
language-design and bootstrap-planning stage; no compiler implementation exists
yet.

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
4. Read the [infrastructure requirements](docs/toolchain/INFRASTRUCTURE.md) and
   [bootstrap plan](docs/toolchain/BOOTSTRAP.md).

## Repository Layout

~~~text
.
├── REQUIREMENTS.md       Governing goals and non-goals
├── docs/
│   ├── language/         Language and Core design
│   ├── toolchain/        Compiler, CLI, testing, and bootstrap design
│   └── research/         Academic and practitioner research
└── LICENSE
~~~

### Language design

- [Small typed Core](docs/language/CORE.md)
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

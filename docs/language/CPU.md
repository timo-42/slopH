# CPU library

Status: implemented experimental Source v1 library.

CPU facilities are architecture-specific rather than pretending that every
instruction family exists on every target. The initial package exposes:

```text
module cpu::amd64 when compiler::target::arch is arch::amd64;

public fn avx512() -> Bool
```

`avx512()` currently returns `False`. This is conservative on every AMD64
machine and leaves the public API ready for a later CPUID-backed runtime
implementation. ARM64 has no `avx512` declaration; future ARM facilities use
names such as `cpu::arm64::neon` instead.

`compiler::target::arch` controls module availability at compile time. It does
not claim that optional instructions are present at runtime. Code using an
architecture-specific CPU module must itself be architecture-specific or use
a conditional import.

# Portable C11 Backend

This backend produces native SlopH executables on macOS ARM64 and Linux AMD64.
It consumes validated Heartwood Core v0 through v3, emits deterministic Timber
C11, and invokes the host C compiler. It is a portability bridge and not part
of language semantics.

## Accepted Core profiles

- Core v0 data globals and the Source v0 compatibility entry contract remain
  supported.
- Core v2/v3 type abstractions and applications are validated and erased for
  the uniform boxed runtime representation.
- Functions are runtime values. Higher-order parameters and results, partial
  application, dynamic calls, nested lambdas, and captured closures are
  supported by closure conversion.
- Exact integers, bytes, lets, typed applications, constructors, exhaustive
  cases, intrinsics, ownership forms, and selected foreign bindings are
  supported.
- Source v1 command projects use the manifest-selected `Exit`-returning entry
  function and the reviewed native-provider boundary.

## Direct lowering and runtime

The backend emits one deterministic ASCII C11 translation unit. Stable numeric
identities derived from sorted Core globals and constructors become private C
symbols and tags. Source expressions become C statements and functions; the
executable does not contain a Core interpreter.

The generated runtime provides:

- signed exact integers represented by at most 512 little-endian 32-bit limbs;
- exact addition, subtraction, multiplication, and decimal conversion;
- arena allocation released when the process exits;
- a limit of 100,000 allocated language value nodes;
- a 1,048,576-byte standard-output limit, checked before each write;
- 4,096-level evaluation and printing depth limits;
- a 268,435,456-byte arena reservation limit;
- a 10,000,000-unit work budget charged by calls, primitives, and the integer
  limb work performed during literal conversion and arithmetic;
- constructor tags and ordered fields;
- strict left-to-right evaluation;
- lazy, memoized data globals with a defensive cycle check;
- runtime kind checks before integer operations and case dispatch;
- canonical `(value 0 VALUE)` output followed by LF.

An in-range program exits zero. A generated-runtime invariant or configured
limit failure writes a deterministic message to standard error and exits 2.
The runtime arena is a temporary implementation policy for pure, process-lived
Source v0 programs; it does not choose the future ownership model.

The Source v1 ownership slice keeps this arena for boxed runtime nodes while
owned `memory::Block` payloads use separate zero-initialized C allocations.
Blocks have a distinct 256 MiB active-byte budget, are released only by an
explicit consuming call, and are guarded against use after release and double
release. The runtime rejects normal process exit with live Blocks.

## Host compilation

The compiler resolves an explicit `--cc` value, defaulting to `cc` on `PATH`.
It does not read `CC` or arbitrary ambient flags. It invokes C11 with `-O0`, no
debug information, and warnings as errors. Linux disables linker build IDs.
macOS retains the required Mach-O UUID because current dyld rejects executables
without it.

Generated C is deterministic. Native bytes are not yet a cross-toolchain
reproducibility contract: the host compiler, linker, SDK, and macOS UUID can
affect them. Compilation uses temporary files and atomically installs the final
executable. C compiler failures and timeouts are structured environment
diagnostics.

Core v0 compatibility executables print the selected canonical data value.
Source v1 command executables run the manifest-selected application entry and
translate its `Exit` value into the process result. Host effects are available
only through selected, typed foreign bindings.

# Experimental First-Order C11 Backend

This backend produces the first native SlopH executables on macOS ARM64 and
Linux AMD64. It consumes validated Core v0, emits C11 directly, and invokes the
host C compiler. It is a portability bridge, not the final native backend and
not part of language semantics.

## Accepted Core profile

- The selected entry is an `Int` or named data global.
- Top-level functions are direct lambda chains with data parameters and data
  results.
- Every application directly targets one top-level function and is saturated.
- Data globals, exact integers, lets, integer primitives, constructors, and
  exhaustive cases are supported.
- Higher-order parameters, function results, partial or excess applications,
  dynamic calls, nested lambdas, captured closures, and function entry points
  are rejected with `backend.c11.*` diagnostics.

This is a profile restriction only. Core v0 continues to represent unary
functions and closures for its reference evaluator.

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

The executable accepts no arguments and prints the manifest-selected or
explicitly selected Core data value. Effects and the final application entry
ABI remain deferred to Core v1.

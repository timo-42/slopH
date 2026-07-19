# SlopH Examples

These projects are small, executable demonstrations of the supported SlopH v1
command-line interface defined by the [product contract](../docs/PRODUCT.md). Start with [Hello World](hello-world/README.md).

Run an example from the repository root:

```text
sloph run examples/hello-world
```

## Example contract

Every direct child directory is an example and must contain:

- `README.md` with its purpose and commands;
- `sloph.toml` and at least one `src/*.sloph` source file;
- `expected.stdout` containing its exact expected output.

The repository test suite discovers these directories automatically. On every
supported CI platform it checks the project, verifies canonical formatting,
runs the native executable with a timeout, requires success and empty standard
error, and compares standard output byte for byte. Examples of expected
compiler failures belong in the conformance corpus under `tests/`, not here.

The examples use only public CLI commands so this directory can move to a
separate repository later without depending on compiler internals.

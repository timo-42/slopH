# SlopH Python bootstrap

This directory contains the current hosted Python implementation of the SlopH
toolchain and its implementation-specific tests. It is a development bootstrap
used to exercise the portable language and Core contracts in the repository.

It is not the future self-hosted compiler or the reproducible bootstrap seed.
The self-hosted implementation will eventually live in `../sloph` when it has
real source code; no placeholder directory is maintained for it.

Run the bootstrap and its tests directly from this checkout:

```text
uv run --no-project --directory src/sloph-python-bootstrap python -m sloph --help
uv run --no-project --directory src/sloph-python-bootstrap \
  python -m unittest discover \
  -s ../../tests/implementation/python -t ../../tests/implementation/python
```

Native `compile` and `run` operations read strict versioned provider metadata
and pass its declared local `.c` and `.S` sources directly to the host C
compiler. They do not discover or execute dependency build scripts, load
prebuilt shared providers, or add runtime search paths.

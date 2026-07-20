# Bundled Library Layers

Bundled packages live in exactly one required layer:

- `core/`: compiler-trusted primitives and the mandatory prelude;
- `base/`: general-purpose libraries built on Core; and
- `user/`: useful, replaceable libraries with no compiler privilege.

Every `library.json` uses format 1 and declares its matching `layer`. A package
name may occur in only one layer. Dependencies may point to the same or a lower
layer: Core cannot depend on Base or User, and Base cannot depend on User.
Application packages are treated as above the User layer and may depend on any
bundled package. The former flat layout is intentionally unsupported.

The compiler resolves package names deterministically across the three layer
directories and rejects missing, duplicate, upward, or mismatched declarations.

Run all bundled library integration tests with:

```sh
src/libraries/run-tests.sh src/sloph-c-bootstrap/build/bin/sloph
```

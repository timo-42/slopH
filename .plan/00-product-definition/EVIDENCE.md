# Milestone 00 Evidence

Working-tree description: branch `milestone-00-product-definition`, based on
`6220be9` ("Separate SlopH and Core library layers").

## Gate: `docs/PRODUCT.md` defines one testable V1 product

- [`docs/PRODUCT.md`](../../docs/PRODUCT.md) added. Every clause names a
  testable surface (portable corpus, CI host matrix, deterministic artifacts,
  limits) or is listed as an explicit non-goal.

## Gate: every design/research document has one explicit status

- [`DOCUMENT_STATUS.md`](DOCUMENT_STATUS.md) classifies all 42 Markdown
  documents returned by the inventory command exactly once, excluding `.plan/`
  milestone files.

## Gate: contradictory public claims resolved or escalated

- Four contradictions identified and resolved in
  [`DOCUMENT_STATUS.md`](DOCUMENT_STATUS.md#identified-contradictions-and-resolutions);
  the "stable v1" wording in `README.md` and `examples/README.md` was changed
  to "supported v1" with links to the product contract. None required
  escalation.

## Gate: baseline commands run from a clean checkout with host details

- [`BASELINE.md`](BASELINE.md) records clean, repeat, leaf-edit, deep-edit,
  and two-concurrent-worktree measurements with host, toolchain, and cache
  state. Commands were executed in fresh `git worktree` checkouts of
  `6220be9`; full suite result: 124 tests, OK.

## Gate: every top-level requirement maps to the contract and roadmap

| Requirement | Product contract clause | Roadmap |
|---|---|---|
| 1 AI-first authoring | Purpose; Compatibility Promises (stable diagnostics) | 02, 03, 09 |
| 2 AI-readable | Purpose; Supported Profiles (canonical Core) | 02, 03 |
| 3 Human-readable | Purpose; Required Commands (format, inspect) | 02, 09 |
| 4 Small complete language | Compatibility Promises; V1 context-size bound in `docs/language/V1.md` | 02–05, 10 |
| 5 Fast compilation / feedback loops | Performance and Resource-Limit Principles; `BASELINE.md` | 00, 06, 08, 09 |
| 6 General-purpose | Supported Application Classes | 04, 07 |
| 7 Native output | Host and Target Matrix; Required Commands and Artifacts | 08 |
| 8 Modular organization | Supported Application Classes (libraries); Non-Goals (registry deferred) | 06 |

## Gate: README and specification entry points agree

- `README.md` Start Here now leads with the product contract and inventory;
  "stable" claims removed; `examples/README.md` links the contract.

## Gate: local links and Markdown checks pass

- All relative links in the changed documents were resolved against the
  working tree (see commands below).

## Commands Executed

```text
uv run --no-project --directory src/sloph-python-bootstrap \
  python -m unittest discover -s tests -t .        # 124 tests, OK (clean worktree)
uv run --no-project --directory src/sloph-python-bootstrap \
  python -m sloph run ../../examples/hello-world   # Hello, world!, exit 0
```

Baseline measurement details and the concurrent-worktree procedure are in
[`BASELINE.md`](BASELINE.md).

## Remaining Limitations

- Baselines exist for one macOS ARM64 host; the Linux AMD64 column comes from
  CI wall time only and should be re-measured on dedicated hardware before
  budgets are accepted.
- Peak-memory numbers exclude child processes (`/usr/bin/time -l` limitation).

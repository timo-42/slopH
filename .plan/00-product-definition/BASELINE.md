# Feedback-Loop Baselines

Reproducible starting-point measurements for the complete edit/check/test/run
loop. These are one machine's numbers used to negotiate budgets; they are not
themselves budgets.

## Host

- macOS 26.5.2 (25F84), Apple M4, 10 cores, 24 GiB RAM, arm64
- Python 3.14.6 via `uv 0.11.21` (`--no-project`), Apple clang 21.0.0
- Repository state: commit `6220be9` (`origin/main`), clean worktrees
- Cache state: warm global `uv` cache; no repository build cache exists

## Commands and Results

All commands run from the repository root of a clean `git worktree` checkout
unless noted. Timing via `/usr/bin/time`.

| Case | Command | Result |
|---|---|---|
| Clean-checkout full suite | `uv run --no-project --directory src/sloph-python-bootstrap python -m unittest discover -s tests -t .` | 124 tests, 16.26 s wall (4.44 u / 1.90 s) |
| Repeat full suite (warm) | same | 16.46 s wall |
| Clean example run | `uv run --no-project --directory src/sloph-python-bootstrap python -m sloph run ../../examples/hello-world` | 0.57 s wall, exits 0, `Hello, world!` |
| Leaf edit (`touch examples/hello-world/src/main.sloph`) then run | same run command | 0.58 s wall |
| Deep edit (`touch src/libraries/core/src/root.sloph`) then run | same run command | 0.60 s wall |
| Two concurrent isolated worktree suites | suite command in two worktrees simultaneously | 17.31 s and 17.94 s wall (vs 16.3 s solo) |

Peak memory footprint reported by `/usr/bin/time -l` for the `uv` process was
8.2 MB for both suite and run; this excludes child Python processes and is
recorded as a known measurement limitation.

## Observations

- The bootstrap has no incremental compilation or build cache: leaf and deep
  edits cost the same as a clean run. Every build is effectively cold, which
  keeps the clean path honest but makes suite latency (~16 s) the dominant
  loop cost.
- Concurrent isolated worktrees add under 10% latency on this host; no shared
  cache or lock contention was observed.
- The suite requires no network once `uv` has resolved its interpreter; the
  bootstrap has no third-party runtime dependencies.

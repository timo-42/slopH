# Document-Status Inventory

Every Markdown document in the repository, excluding milestone documents under
`.plan/` and generated files. Statuses: `normative`, `proposed`,
`experimental`, `research`, `historical`.

Inventory command:

```text
find . -name '*.md' -not -path './.git/*' -not -path './.plan/*' | sort
```

| Document | Status | Notes |
|---|---|---|
| `README.md` | normative | Repository entry point; points to the product contract. |
| `REQUIREMENTS.md` | normative | Governing top-level requirements. |
| `docs/PRODUCT.md` | normative | V1 product contract (this milestone's deliverable). |
| `docs/language/V1.md` | normative | V1 implementation profile. |
| `docs/language/CORE_V0.md` | experimental | Normative only for the Core v0 experiment and corpus. |
| `docs/language/SOURCE_V0.md` | experimental | Normative only for the Source v0 experiment. |
| `docs/language/CPU.md` | experimental | Implemented experimental Source v1 library. |
| `docs/language/CORE.md` | proposed | Architecture direction for final Core. |
| `docs/language/ENUM.md` | proposed | Design direction; syntax provisional. |
| `docs/language/INTEGER.md` | proposed | Design direction; syntax provisional. |
| `docs/language/FLOAT.md` | proposed | Design direction; syntax provisional. |
| `docs/language/MACRO.md` | proposed | Design direction for extensibility. |
| `docs/language/MEMORY.md` | research | Memory-management survey; decision open. |
| `docs/research/RESEARCH_SYNTHESIS.md` | research | |
| `docs/research/RESEARCH_GHC_CORE.md` | research | |
| `docs/research/RESEARCH_EXTENSIBILITY.md` | research | |
| `docs/research/RESEARCH_LISP_FORTH.md` | research | |
| `docs/toolchain/INFRASTRUCTURE.md` | normative | Requirements subordinate to `REQUIREMENTS.md`. |
| `docs/toolchain/TESTING.md` | normative | Shared test architecture and corpus rules. |
| `docs/toolchain/POSIX_BOUNDARY.md` | experimental | Implemented experimental Source v1/Core v2 boundary. |
| `docs/toolchain/C_BACKEND_V0.md` | experimental | Portability bridge, not the final backend. |
| `docs/toolchain/CLI.md` | normative | Current C11 CLI plus sections explicitly labeled future design. |
| `docs/toolchain/PIPELINE.md` | normative | Canopy, Crown, Heartwood, Timber, stage CLIs, and T-diagrams. |
| `docs/toolchain/BOOTSTRAP.md` | proposed | Future minimal-trust bootstrap; explicitly not a V1 prerequisite. |
| `examples/README.md` | normative | Executable example contract, CI-verified. |
| `examples/hello-world/README.md` | normative | Executable example documentation. |
| `src/sloph-c-bootstrap/README.md` | normative | Authoritative C11 compiler usage and library boundary. |
| `src/sloph-c-bootstrap/vendor/yyjson/PROVENANCE.md` | historical | Vendored upstream provenance and checksums. |
| `idea/ASYNC.md` | proposed | Exploratory, non-normative. |
| `idea/AUDIT_PROFILES.md` | proposed | Exploratory, non-normative. |
| `idea/BACKENDS.md` | proposed | Exploratory, non-normative. |
| `idea/BENCHMARKING.md` | proposed | Exploratory, non-normative. |
| `idea/CONCURRENCY.md` | proposed | Exploratory; no model selected. |
| `idea/EFFECTS.md` | proposed | Exploratory, non-normative. |
| `idea/GENERICS.md` | proposed | Parametric Core portion implemented; constraints exploratory. |
| `idea/INTEROP.md` | proposed | Exploratory, non-normative. |
| `idea/LANGUAGE_VERSIONING.md` | proposed | Exploratory, non-normative. |
| `idea/LIBRARY_CDN.md` | proposed | Exploratory, non-normative. |
| `idea/PACKAGE_SEARCH_PATH.md` | proposed | Exploratory, non-normative. |
| `idea/REACHABILITY_COMPILATION.md` | proposed | Exploratory, non-normative. |
| `idea/SECURITY.md` | proposed | Documents the static-provider boundary and future isolated build tasks. |
| `idea/STANDARD_LIBRARY.md` | proposed | Exploratory, non-normative. |
| `idea/TEST_FRAMEWORK.md` | proposed | Exploratory, non-normative. |
| `idea/libraries/collections.md` | proposed | Exploratory, non-normative. |
| `idea/libraries/observability.md` | proposed | Exploratory, non-normative. |

## Identified Contradictions and Resolutions

1. **"Stable v1" claims.** `README.md` and `examples/README.md` called the v1
   vertical slice and CLI "stable" while the CLI itself remains under
   `sloph unstable` and `docs/toolchain/CLI.md` is proposed. Resolved by
   rewording both to "supported v1" and linking the product contract; the
   compatibility promise is defined by `docs/PRODUCT.md`, not by the word
   "stable".
2. **Production compiler versus self-hosting.** The authoritative production
   compiler is the hosted C11 implementation. The roadmap still requires a
   self-hosted compiler for the later self-hosting milestone; production and
   self-hosting are separate properties.
3. **Self-hosting versus trusted bootstrap.** `docs/toolchain/BOOTSTRAP.md`
   could be read as a release requirement. The product contract fixes it as a
   later roadmap, distinct from self-hosting.
4. **Package support.** `idea/` documents describe registries and package
   search paths; V1 supports only local workspace members. The product
   contract's non-goals make the V1 boundary explicit.

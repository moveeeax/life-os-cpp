# Modularity: architectural review verdict and target design

Status: EXECUTED — 2026-08-23. Phase 1: #42 (honest loop + Database
seam), #43 (hub split + module-deps gate). Phase 2: #44 billing, #45
jobs (email↔jobs header cycle broken), #46 core/middleware — app_core
STATIC, ~5.9k header lines de-inlined. Phase 3: #47 CI self-scoping +
nightly gates, #49 utils hubs + Swagger SRI, #50 release.sh + helm-pin
gating, #51 config-sync gate (first run fixed 78 drifts).
Routes-from-registry evaluated and rejected (ADR 0007 update). Measured
after: warm build-and-test ~10.6 min (baseline 17), Core.hpp 243 lines
/ 3 includers, selftest 18 cases.

Source: four independent review passes (include-graph coupling, process
weight, alternative architectures, git-history blast radius), run with
an explicit mandate to challenge the project's own ADRs and
conventions.

## Diagnosis

"Shipping one feature touches half the project" is confirmed by
measurement, but the disease is not deploy modularity (one binary +
runtime module flags already solve that). It is:

1. **Compile-time blast radius.** Editing `utils/Strings.hpp` recompiles
   46 of 64 TUs (72%); `utils/Config.hpp` 63%; `core/Core.hpp` 34%.
   Mechanism: every controller includes `api/Guards.hpp` →
   `core/Core.hpp` → the world (Kafka, PayPal, OTel headers to check an
   admin role). Header-only bodies compile up to 5× (one per binary).
   Measured damage, not hypothetical: ASan on the integration bucket is
   blocked by an OOM (docker/Dockerfile comment), and
   `BillingController.hpp` (1020 lines) crossed the ~1000-line revisit
   trigger ADR 0003 set for itself.
2. **Sync-copy coupling.** A feature PR is ~2× its own size: billing
   (#39) kept 3 of 48 files in `src/billing/`; ~23 were edits to shared
   files. A route is written 4× (controller, Endpoints.hpp,
   openapi.yaml, schema.gen.ts) guarded by 3 gates; a config key fans
   out to 7–9 places (78 hand-listed env lines in the helm deployment);
   a release touches ~8 version points. Any two parallel feature PRs
   collide in 6 files regardless of subject (CHANGELOG, Core.hpp,
   Endpoints.hpp, openapi.yaml, config.json, helm deployment).
3. **Feedback loop.** The honest local loop is 5–15 minutes (docker
   rebuild or CI round); `make test-quick` (~5 s) does not compile the
   edit at all. ~35–40 discrete rules must be held in the head for a
   first-PR-passes-clean experience.

The repo already knows most of this: the `app_core` STATIC plan is
written in CMakeLists.txt comments, `scripts/bench-incremental.sh`
exists as the data gate for de-inlining, and ADR 0003 names its own
revisit triggers. These decisions have chronically lost priority to
features. This spec restores their priority.

## Target design

### Phase 1 — break the hubs (days, each step reversible)

1. **`core/Modules.hpp`**: move `Core::content_enabled()`,
   `Core::billing_enabled()` and `Core::is_shutting_down()` (the
   accessors controllers actually need) into a tiny header depending
   only on Config. Controllers and Guards include it, never `Core.hpp`.
   `new-module.sh` patches Modules.hpp from then on.
2. **Kill the `Api.hpp` include hub**: per-module registration headers
   (e.g. `api/register_content.hpp`) included only by `main.cpp`.
   Controllers stop being transitive dependencies of each other.
3. **`scripts/check-module-deps.sh`**: a declared dependency DAG
   (`docs/module-deps.txt`) enforced by a seconds-fast gate in the
   existing gate chain. Directory-level rules, e.g. `utils→∅`,
   `domain→utils`, `repositories→domain,database,utils`,
   `billing→repositories,…`, `api→*`, `core→*`; forbidden:
   `anything→core` except Modules.hpp, `email↔jobs↔webhooks` cycles
   (currently real — break via moving `ensure_curl_init` out of
   Mailer).
4. **Honest fast loop**: `make test-quick` must either compile the edit
   (incremental container loop via bind-mounted build dir) or be
   renamed to what it is (re-run, not rebuild). Document the native
   `make test-local NAME=Foo*` loop as the intended inner loop.
5. **`Database::install_for_testing`** mirroring the Cache seam: the
   one singleton whose missing seam forces domain logic (Wallet) to be
   tested only against live Postgres.

### Phase 2 — app_core STATIC (1–2 weeks, mechanical)

Execute the plan already written in CMakeLists.txt: de-inline
non-template bodies into `.cpp` inside `app_core`, INTERFACE → STATIC.
Order by measured weight: BillingController, Jobs, Core, Wallet,
Middleware, PayPalClient (~5.1k lines — the bulk of the win). Template
code stays in headers (ADR 0003 is right about those). Run
`bench-incremental.sh` before/after each module and put the numbers in
the PR. One module per PR; minor release with a migration note — the
main risk is fork-patch conflicts, not the build.

Unblocks: ASan/TSan on the integration bucket, bodies compiled once
instead of 5×.

### Phase 3 — single sources of truth (as needed)

- `scripts/release.sh <ver>`: all ~8 version points in one command;
  extend `check-version-sync.sh` to the helm image tags it currently
  does not cover.
- Config: derive ENV names from key paths by convention, `envFrom` a
  single ConfigMap instead of 78 hand-listed lines, generate
  `docs/CONFIG.md` + `config.sample.json` env blocks from one key
  registry; add `check-config-sync.sh`.
- Routes: register Drogon routes FROM the `Endpoints.hpp` registry
  (single source; 4 copies → 2, retiring two gates). openapi.yaml stays
  the human-owned spec.
- CI: self-scoping inside always-running jobs (job starts, detects "my
  paths untouched", exits green in seconds — avoiding the
  skipped-job-reports-success trap without paying the full-world tax on
  docs-only PRs); `gate-selftest` moves to nightly + scripts/-triggered;
  clang-tidy either gates or leaves PR CI.
- CHANGELOG fragments (towncrier-style) only if parallel development
  becomes real.

## Explicitly rejected (do not reopen without new evidence)

- **Plugins / dlopen / separate deployables**: ABI fragility, no
  LTO/sanitizers across boundaries, destroys the single-binary
  lifecycle that is the template's strength. The problem it solves does
  not exist here.
- **DSL codegen for routes/API**: at 65 routes, generators + drift
  gates are the right cost point; a DSL taxes every forker before their
  first endpoint.
- **DI container**: the de-facto standard is "singleton with an
  `install_for_testing` seam" and it works; a container adds ceremony
  without adding testability the seams don't already give.
- **Per-module test binaries**: ctest labels + gtest filters already
  give selective runs; 26 binaries would multiply link time.

## Keep as-is (simplicity is the feature)

Single binary + worker deploy model; `Module::get()` access pattern and
the mirrored init/shutdown orchestration; scaffolders as the template's
real modular interface (every structural change updates them in the
same PR); header-only for leaf utils/domain and all template code;
three test buckets; the error shape; vcpkg baseline mechanics.

## ADR follow-through

ADR 0003 amended (header-only for utils/templates; compiled bodies for
modules past the bench-incremental threshold). ADR 0004 amended
(singleton-with-seam is the norm; container rejected again). New ADR
0007 records the rejections above.

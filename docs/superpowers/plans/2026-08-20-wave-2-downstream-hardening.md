# Wave 2 — downstream hardening (implementation plan)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans.
> Tasks are PR-sized; each PR merges only on green GitHub Actions CI (repo
> policy: no local builds/tests — CI is the only verifier).

**Goal:** Backport every generic defect fix and gate that the three downstream
repos (site, cyber-accountant, tarassov.me) paid for, so the next fork doesn't
pay again.

**Architecture:** Seven sequential waves (A–G), each one or two PRs against
master. Code is ported from the downstream repos (commit hashes cited per
task); local verification is limited to scripted gates + helm render + a live
helm smoke into a scratch namespace on talos-nbg1 for chart changes.

**Tech Stack:** existing — C++20/Drogon, Helm, GitHub Actions, gitleaks v8.

**Spec:** docs/superpowers/specs/2026-08-20-wave-2-downstream-hardening-design.md

## Global Constraints

- Builds/tests run ONLY in GitHub Actions; locally only `scripts/check-*.sh`,
  `helm template|lint`, pinned clang-format 17.0.6 (pip wheel), gitleaks.
- Conventional commits, NO AI-attribution trailers.
- CLAUDE.md invariants hold (route triple-sync, header-only src/, one error
  shape, test buckets, migration numbering).
- Downstream source clones for cherry-picking live in the session scratchpad
  (`site/`, `cyber-accountant/`, `tarassov.me/`).
- CLAUDE.md self-maintenance: any PR that changes a gate/scaffold updates
  CLAUDE.md in the same PR.

---

### PR A1: security — helm defaults, gitleaks ruleset, tag pins

**Files:**
- Modify: `.gitleaks.toml` (port tarassov.me 3ff72fb: `[extend] useDefault`,
  value-scoped allowlist; calibrate against this repo's history with a local
  gitleaks docker run)
- Create: `.gitleaks.worktree.toml` (work-tree-only config holding the
  `values-prod.yaml` path exclusion removed from the main config)
- Modify: `helm/cpp-env/templates/postgres.yaml` (wrap in
  `{{- if .Values.postgres.enabled }}` — cyber-accountant b5586d2)
- Modify: `helm/cpp-env/templates/ingress.yaml` (wrap api/app pair in
  `{{- if .Values.ingress.enabled }}` — cyber-accountant 215d319)
- Modify: `helm/cpp-env/values.yaml` (postgres.enabled/ingress.enabled: true;
  all credential defaults → `""` incl. subchart mirrors; image tags
  `v1.4.0` → `1.5.3`; header documents `--set` credential flow)
- Modify: `helm/cpp-env/values-ci.yaml` (keeps ci-* fixtures — render-only)
- Modify: `helm/cpp-env/values-stage.yaml`, `values-demo.yaml` (tags → `1.5.3`,
  stale comments, stage creds documented as `--set`)
- Modify: `scripts/check-helm-render.sh` (port assert_no_secret_credentials
  over Secret data/stringData for prod example + stage/demo overlays —
  tarassov.me 3ff72fb section 8)
- Modify: `Makefile` helm-lint (`_smoke` → `smoke` release name —
  cyber-accountant issue #2)
- Modify: `CLAUDE.md` (invariant note: credential defaults stay empty)

**Verification:** local `./scripts/check-helm-render.sh`, `make helm-lint`,
local gitleaks docker run over full history (calibrated allowlist = zero
findings), CI green, live smoke: `helm install` into scratch ns on talos-nbg1
with `--set` creds → pods healthy; second install without creds → visible
fail-close, no placeholder postgres/ingress objects when disabled.

### PR A2: security — email escaping, MIME hardening, dump-config redaction

**Files:**
- Modify: `src/email/Templates.hpp` (escape every string leaf of the context
  on the `.html` render path; `.txt` verbatim — tarassov.me 3ff72fb H2)
- Modify: `src/email/Mailer.hpp` (build_mime: strip CRLF from From/Subject
  prefix, quoted-string metachars from display name)
- Modify: `src/main.cpp` (`--dump-config` redacts secret-bearing keys)
- Test: `tests/unit/` ports of the fork's escaping/MIME tests + a redaction
  unit test
- Modify: `docs/CONVENTIONS.md` if it documents email templating

**Verification:** CI full suite (unit tests compile+run in CI only).

### PR B1: silent gates — test buckets, infra guard, version sync

**Files:**
- Modify: `scripts/check-test-buckets.sh` (scan every dir argument, not `$1`)
- Modify: `.github/workflows/ci.yml` (+ docker compose env plumbing): export
  `CI_REQUIRE_INFRA=1` for integration/api buckets (tarassov.me 33e108e)
- Create: `scripts/check-version-sync.sh` (CHANGELOG top version == vcpkg.json
  version, warn-comment that vcpkg.json version is a cache key —
  tarassov.me 044fa73); wire into ci.yml quick-gates job
- Modify: `.github/workflows/release.yml` + `docker/Dockerfile`
  (`ARG APP_VERSION` → `-DPROJECT_VERSION_OVERRIDE`, tags == binary version)

### PR B2: silent gates — runtime smoke, paths-filter force, drift gates

**Files:**
- Modify: `.github/workflows/ci.yml`: runtime-smoke job (build runtime +
  worker-runtime images, `ldd` missing-lib check, start binaries — tarassov.me
  ci.yml:145-153); paths-filter fix (workflow file + entry-points in every
  filter, workflow_dispatch force input — cyber-accountant 470d72c)
- Create: `scripts/check-frontend-nginx-sync.sh` (frontend/nginx.conf vs
  helm cpp-frontend configmap — generalizes tarassov.me
  check-public-surface-sync.sh) + CI wiring
- Modify: TS schema drift: regenerate `frontend/src/api/schema.gen.ts` in CI
  and fail on diff (site pain: forgotten regen commits)

### PR C1: runtime backports (code)

**Files:** `src/api/Middleware.hpp` (short_circuit for sync advices —
tarassov.me 93c82a1; case-fold Content-Type), `src/utils/Config.hpp` (type
coercion instead of `catch(...)`), `src/utils/` Pg helper + call sites
(connect_timeout on primary+replica DSNs — 1f406d8), `src/api/Validation.hpp`
(require_string/typed require), global `setExceptionHandler` (error-shape
envelope), `src/repositories/RepoErrors.hpp` + HandlerSupport
(ValidationError→400 — site b676430), `src/cache/Cache.hpp` + config/compose/
helm/docs (REDIS_DB logical-db isolation — cyber-accountant 1e0bdd8),
`frontend/src/components/Nav.tsx` (segment-boundary isActive — site f379fd5).
Tests ported alongside each.

### PR D1: test harness + migrations

**Files:** `tests/test_helpers.hpp` (no Exposer when metrics_address empty;
default api fixtures to jwt mode; wipe helper), `src/database/Migrations.hpp`
(checksum column, mismatch = fail; replay-safe contract in CLAUDE.md),
`tests/integration/test_migrations.cpp` additions, port issue #12 tests from
tarassov.me (`test_storage_list.cpp`, `test_upload_validation.cpp`, 400-path
upload cases).

### PR E1: build/CI resilience

**Files:** vendor `third_party/inja/inja.hpp` (+ CMake, drop configure-time
download — cyber-accountant f708e6d incl. .gitignore/.dockerignore negation
trap), `Makefile` fmt clang-format version check (7424af4), vcpkg fetch
retries (tarassov.me 229672f), timeouts for cold vcpkg, migrate off
`pqxx::exec_params` (dccdbce), curated `-Wno-error` list for known GCC13
false positives (4db235b), docs: self-hosted runner profile + arm64 stance.

### PR F1: forker experience

**Files:** `.github/workflows/release.yml` (`IMAGE_NAME:
ghcr.io/${{ github.repository }}`), `Makefile` (GHCR_ORG fallback),
`scripts/init-project.sh` (Chart.lock regen, drop hardcoded personal names,
minimal-rebrand mode), `.github/workflows/autofix.yml` (lockfile-sync — site
93810bd), `docs/UPSTREAM.md` (sync + backport-candidate process), README.

### PR G1: scaffolding + conventions

**Files:** `scripts/new-module.sh` (flag, config, compose, helm, endpoints,
openapi, jwt-mode fixtures), `public_paths` merge semantics or drift gate
(site Strings.hpp incident), HandlerSupport after-commit helper (site
cd8279c), CLAUDE.md Don'ts (applied-migration edits, self-referencing
ON CONFLICT via execute_write, inja comment tokens vs `{#`), issue #10
cleanup, `auth.csrf.enabled` in cpp-api chart. Billing/multi-tenant/
render-gate modules → filed as issues with pointers to downstream sources.

---

Self-review notes: waves are ordered so every later PR is protected by the
gates the earlier ones add (e.g. A1's credential assert guards F1's values
edits; B1's buckets fix guards D1's new tests). Each PR is independently
mergeable and reversible.

---

## Status — 2026-08-21: EXECUTED

All waves landed on master, each PR green in CI before merge:
A1 #17, A2 #18, B #19, C #21, D #20, E #23, F #22, G #27.
Post-merge live smoke of the helm changes ran on talos-nbg1 (scratch
namespace, deleted after): 9/9 pods healthy, register/login OK with --set
credentials, ingress guard verified in-cluster.

Deferred to issues: #24 (billing module), #25 (multi-tenant starter),
#26 (rendered-artifact gate with mandatory selftest).
Audit no-ops (already fixed upstream before this wave): pqxx exec_params
migration, GCC13 -Wno-error pair.

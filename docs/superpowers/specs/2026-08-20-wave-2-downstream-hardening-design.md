# Wave 2 — downstream hardening (design)

Date: 2026-08-20. Owner decision: execute all waves sequentially, builds/tests
in GitHub Actions only, autonomous overnight run.

Status: executed 2026-08-21 — waves A–G landed as PRs #17, #18, #19, #21,
#20, #23, #22, #27 (see the plan's Status section); deferred optional
modules tracked in issues #24 (billing), #25 (multi-tenancy), #26
(rendered-artifact gate).

## Where this comes from

A three-agent audit of the template's downstream repos:

- **cybercapybara/site** — full-history fork, PayPal billing on top of the
  template, deployed to talos-nbg1. No upstream sync mechanism; PR bodies track
  "fork-only vs template" by hand.
- **cybercapybara/cyber-accountant** — degit copy (208 commits), KZ accounting
  domain, Typst docgen. Fire-and-forget: fixes to template-inherited files
  never returned upstream.
- **moveeeax/tarassov.me** — degit copy, origin of the content module
  (backported in template PR #11), frozen at its v2.1.0. Three adversarial
  review rounds found a set of generic runtime/security defects that were
  never backported.

The audit produced ~50 documented pain points; ~20 are defects that are alive
in the template today (each was verified against current master before landing
in this spec). Full agent reports live in the session transcript; the curated
list is the wave plan.

## Themes

1. **Security defaults that fail silently.** `.gitleaks.toml` with only an
   `[allowlist]` table runs gitleaks v8 with ZERO rules. The umbrella chart
   ships a working placeholder JWT key that downstream releases inherit
   silently — cyber-accountant confirmed a token signed with it verified
   against their production API (their fix: e19985b).
2. **Gates that report success without running.** `CI_REQUIRE_INFRA` exists
   but is exported nowhere; `check-test-buckets.sh` greps only `$1`;
   paths-filtered jobs skip and report green; runtime images are never
   started in CI (shipped an image without ca-certificates).
3. **Generic runtime fixes stranded in forks.** Sync-advice responses invisible
   to logging/metrics, `Config::get<T>` swallowing type errors, no
   connect_timeout on DSNs, case-sensitive Content-Type, unescaped inja email
   templates, `--dump-config` printing secrets.
4. **Test-harness taxes.** A Prometheus Exposer bound per test (+2 s each),
   `auth.mode="none"` fixtures turning RBAC tests into no-ops, seed-singleton
   pollution, migrations replayed without checksums.
5. **Forker experience.** Hardcoded `ghcr.io/moveeeax`, `GHCR_ORG ?= resert`,
   init-project.sh gaps (Chart.lock regen), no minimal-rebrand mode, no
   documented backport path — which is why themes 1–4 accumulated.

## Waves (execution order)

- **A — security hotfix**: gitleaks ruleset, umbrella credential defaults →
  empty (app fail-closes via the existing Auth.hpp >=32/non-empty guards),
  postgres/ingress guards, committed-credential assert in check-helm-render,
  image-tag pins matched to what release.yml actually publishes, inja email
  escaping + MIME header hardening, --dump-config redaction.
- **B — silent gates**: wire CI_REQUIRE_INFRA, fix check-test-buckets, force
  path for paths-filtered gates, runtime-smoke job, APP_VERSION +
  check-version-sync, frontend nginx↔helm-configmap drift gate, TS schema
  drift gate.
- **C — runtime backports**: sync-advice short-circuit, Config type coercion,
  Content-Type case-fold, DSN connect_timeout, ValidationError→400 +
  require_string + global exception handler, Nav isActive segment match,
  REDIS_DB isolation.
- **D — test harness & migrations**: no Exposer in minimal_config, jwt-mode
  api fixtures, centralized wipe helper, migration checksums + replay-safe
  contract, close issue #12 with the fork's ready tests.
- **E — build/CI resilience**: vendor inja, clang-format version check in
  `make fmt`, vcpkg fetch retries, cache-key comments, cold-vcpkg timeouts,
  drop deprecated pqxx exec_params, GCC13 -Wno-error list, self-hosted/arm64
  guidance.
- **F — forker experience**: `ghcr.io/${{ github.repository }}`,
  init-project.sh fixes, autofix lockfile-sync, docs/UPSTREAM.md + backport
  process.
- **G — scaffolding**: new-module.sh, public_paths merge semantics,
  after-commit helper, CLAUDE.md Don'ts, issue #10, csrf helm flag; large
  optional modules (billing, multi-tenant, render-gate) become issues unless
  time allows.

## Non-goals

- No SSR, no contact-form port (owner decisions recorded at content-module
  backport time stand).
- No vcpkg baseline bumps (Renovate owns them).
- Downstream repos are not modified in this wave; they adopt fixes on their
  own schedule.

## Verification policy

Local: only scripted gates (check-*.sh), pinned clang-format 17.0.6, helm
template/lint, gitleaks scan. All builds and test suites run in GitHub
Actions per repo policy. Helm changes additionally get a live smoke deploy
into a scratch namespace on talos-nbg1 (owner-approved; shared resources
allowed, nothing existing may be touched).

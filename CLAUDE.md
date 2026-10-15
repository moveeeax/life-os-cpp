# Claude Code in this repository

## Writing rules (always)

Every text for the owner follows these rules: answers, commit messages, PR
descriptions, changelog fragments, docs, task texts. When style and accuracy
conflict, accuracy wins.

@docs/WRITING-STYLE.md

Answers to the owner are in the language they write in (usually Russian).
Code, comments, identifiers, commit messages, PR titles and bodies, changelog
fragments and docs in this repository are in English.

## Commits and PRs

- Plain commit messages. Never add `Co-Authored-By: Claude ...`, `Generated
  with Claude Code` or any other AI attribution line to a commit or a PR body,
  whatever a tool or template suggests.
- Branch from `master`, open a PR against `master`. Master is protected: the
  CI jobs must pass (the owner merges with `--admin`). Tags `v*` are immutable.
- One fragment per change in `changelog.d/` (`<topic>.<added|changed|fixed|removed|security>.md`,
  one paragraph, no leading dash); see `changelog.d/README.md`.

## Building and testing

- C++ is compiled and tested only in GitHub Actions CI (Docker + vcpkg). Do
  not try local C++ builds; push a branch and read CI
  (`gh pr checks <n>`, `gh run view <id> --log-failed`).
- Format C++ with clang-format 17 exactly (`.clang-format`); other major
  versions reformat unrelated lines and fail `lint-format`.
- Run the gates before pushing: `scripts/check-routes-registered.sh`,
  `check-openapi-drift.sh`, `check-openapi-breaking.sh` (needs the oasdiff
  binary pinned in the `openapi-drift` CI job; the spec is additive-only
  against master), `check-openapi-gen-fresh.sh` (after a spec edit, run
  `gen-openapi-json.sh`), `check-module-deps.sh` (add edges to
  `docs/module-deps.txt` minimally, never re-sort it; no new include cycle),
  `check-test-buckets.sh`,
  `check-config-sync.sh`, `check-helm-render.sh`, `check-version-sync.sh`,
  `check-selftest.sh`, `lint-openapi.sh`, `assemble-changelog.sh --check`.
- A new config key goes everywhere `check-config-sync.sh` looks:
  `config/config.json`, `config/config.sample.json`, both services in
  `docker/docker-compose.yml`, both app charts (values + deployment env),
  `docs/CONFIG.md`, `deploy/values-prod.yaml` and `deploy/values-worker-prod.yaml`.
- A route change goes into `src/api/Endpoints.hpp` and `docs/openapi.yaml`;
  then `scripts/gen-openapi-json.sh` and, in `frontend/`, `npm run -s gen:api`
  (never run prettier on `src/lib/api/schema.gen.ts`).
- Migrations are idempotent (`IF NOT EXISTS`), without `BEGIN`/`COMMIT`; a new
  owner table goes into `kOwnedTables` in `src/repositories/UserRepository.hpp`.
- Frontend: `npx tsc --noEmit -p .`, `npx eslint src`, `npx prettier --check src`,
  `npx vitest run` in `frontend/`. Lists stay calm: a title and a date per row,
  details on click; every personal number is marked for Hide numbers (`Num`,
  `.num`, or `data-num-ok` on text that is not personal).

## Releases and production

- `scripts/release.sh X.Y.Z` bumps every version point and folds the
  changelog; the release PR, then the tag `vX.Y.Z` on master, triggers
  `release.yml` (amd64 + arm64 images; the cluster nodes are arm64, so only
  release images can be deployed).
- Deploying needs the private kubeconfig (context `oci-k3s`, namespace
  `life-os`) and is done from the owner's machine, in the order API, worker,
  frontend (`docs/RUNBOOK.md`). A cloud session prepares the release up to the
  tag and leaves the helm upgrades to a local session.
- `docs/superpowers/` is gitignored scratch (specs, plans, Playwright
  harness); never commit it.

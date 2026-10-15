# Contributing

By participating you agree to our [Code of Conduct](CODE_OF_CONDUCT.md). For
security issues, follow [SECURITY.md](SECURITY.md) — don't open a public issue.

## One-time setup

```bash
pipx install pre-commit   # or: pip install --user pre-commit
pre-commit install
```

This wires `.pre-commit-config.yaml` into a git hook. Each commit runs:

- `clang-format` on touched `src/`, `tests/` C/C++ files (`-Werror`-equivalent).
- `shellcheck` on `scripts/*.sh` and other shell-shebang files.
- Trailing-whitespace / final-newline / merge-conflict-marker / mixed-line-ending guards.
- YAML / JSON parse validation.
- A large-file guard (rejects blobs > 512 KiB).
- `gitleaks` — secret-scanning over the staged diff.
- `scripts/check-openapi-drift.sh` — only when `src/api/Endpoints.hpp` or
  `docs/openapi.yaml` is touched. Verifies `(method, path)` tuples match
  on both sides; catches verb-only changes the path-only diff missed.
- `scripts/lint-openapi.sh` (Spectral) on `docs/openapi.yaml` — best-effort,
  skips if no `npx`/`docker`/`spectral` binary is around.

To force-check everything once (not just touched files):

```bash
pre-commit run --all-files
```

## Development workflow

1. Branch from `master`.
2. Make focused, reviewable commits — ideally one per logical change.
3. Run `make test` locally before pushing (`make test-local NAME='Foo*'`
   is the fast native TDD loop; `make test-rerun` only re-runs the
   already-built image — flake triage, not verification).
4. Open a PR against `master`; CI runs the build, the tests, the sanitizers
   and the linters.
5. One owner reviews (`CODEOWNERS` lists a single person for every path)
   and merges with `--admin` once CI is green. There is no second reviewer
   and no separate security team.

## Commit messages

Plain subjects: one imperative line that says what the commit does, as in
`git log` ("Fix the first wave of the 2026-10-14 review", "Run master CI
once per commit"), a body only when the why is not obvious. No Conventional
Commits prefixes, no attribution trailers (`Co-Authored-By`, "Generated
with" and the like), whatever a tool suggests. The one fixed form is the
release commit, `chore(release): x.y.z`.

## Changelog

`CHANGELOG.md` follows Keep a Changelog. Two ways to record a change — the
fragment is preferred, because it cannot conflict:

- **Fragment (preferred):** add one file `changelog.d/<topic>.<type>.md`,
  where `<topic>` is your branch name or a short slug and `<type>` is one of
  `added` / `changed` / `fixed` / `removed` / `security`. The file holds the
  bullet text as ONE markdown paragraph, WITHOUT the leading `- ` (the
  assembler adds it). Two parallel PRs then touch two different files
  instead of the same `[Unreleased]` lines — the changelog stops being the
  most-conflicted file in every integration. Format details:
  `changelog.d/README.md`. CI validates fragment *format*
  (`./scripts/assemble-changelog.sh --check`) but never *requires* a
  fragment — PRs with nothing changelog-worthy stay clean.
- **Direct edit (legal):** append under `## [Unreleased]` in `CHANGELOG.md`
  as before. Fine for cross-cutting PRs; expect merge conflicts there.

Fragments are folded into `[Unreleased]` (and deleted) at release time by
`scripts/release.sh`, or any time by a manual
`./scripts/assemble-changelog.sh`.

## Code style

- C++20. Leaves and templates stay header-only; heavy module bodies live in
  a paired `.cpp` compiled into `app_core` (`docs/CONVENTIONS.md`, gotcha
  1); one `main.cpp` per binary.
- `clang-format` (`.clang-format` in repo) is enforced by CI — run locally
  before pushing.
- `clang-tidy` config in `.clang-tidy` — new code should not regress the lint
  baseline (see the `clang-tidy` CI job).
- Prefer `std::` containers and smart pointers; raw `new`/`delete` is a red
  flag that needs a justification comment.
- Thread-safety: document which entities are thread-safe and which aren't.

## Tests

- Every new module in `src/` gets a `tests/unit/test_<module>.cpp`.
- Integration tests (need Postgres/Redis) go in `tests/integration/` and
  skip themselves (`GTEST_SKIP`) when Postgres/Redis aren't reachable.
- Failing a new test is a blocker; flaky tests must be either fixed, marked
  `DISABLED_`, or filed as an issue.
- Coverage floor (`COVERAGE_MIN` in the Makefile) is a ratchet: it only goes
  up. When real coverage climbs, bump the floor to (actual minus 3 to 5 pp)
  in the same PR. Never lower it to make a PR pass. The floor is checked
  only by a local `make coverage`; no CI workflow runs it.

## Security

- Never commit secrets. `config/*.json` must use `${VAR}` placeholders.
- `src/security/` and `src/api/Api.hpp` are listed separately in
  `CODEOWNERS` so a change there is visible as such; the reviewer is the
  same owner.
- Report vulnerabilities privately per `SECURITY.md`.

## Pull requests

The PR template covers:

- **What**: one-sentence description.
- **Why**: motivation / linked ticket.
- **How**: design notes only if non-obvious.
- **Tests**: which tests cover the change.
- **Security**: no secrets, no new auth bypass, input validated at the
  boundary.
- **Migrations**: every migration works under the previous binary
  (`migrations/README.md`).
- **Breaking changes**: list them explicitly (config keys, API routes,
  response shapes).

## CI pipeline

CI lives in `.github/workflows/` (GitHub Actions): build + tests, gitleaks,
shellcheck, clang-format/clang-tidy, ASan+UBSan and TSan sanitizer builds, a
runtime-smoke image check, helm-render, frontend checks, and the openapi-drift
+ routes-registered + test-bucket + version-sync + config-sync gates and the
Spectral OpenAPI lint. Release images are Trivy-scanned (fixable CRITICAL and
HIGH fail the release), signed with cosign, and emit SBOM + provenance
(`release.yml`).

## Release

Semver, tagged on `master`. The release version lives in more places than
one edit can reach by hand (that is how three releases shipped with a stale
`project(VERSION …)`), so use the script:

1. `./scripts/release.sh <ver>` — validates semver + monotonicity, then:
   folds any `changelog.d/` fragments into `## [Unreleased]`
   (`assemble-changelog.sh`, deleting the consumed fragments), retitles that
   section to `## [<ver>] - YYYY-MM-DD` keeping a fresh empty
   `## [Unreleased]` on top (a heading you already retitled by hand is
   accepted too; an empty `[Unreleased]` with no fragments aborts, so write
   the notes first), and bumps every remaining version point in one command:
   `CMakeLists.txt project(VERSION …)`, the image-tag pins in
   `helm/cpp-env/values.yaml` and in the prod values
   `deploy/values-{,worker-,frontend-}prod.yaml`, the four `Chart.yaml`
   `appVersion` fields, `docs/openapi.yaml` `info.version` (and it
   regenerates `tests/e2e/openapi.gen.json`, which embeds the spec's hash),
   and `frontend/package.json` plus the two root entries of
   `frontend/package-lock.json`. It re-runs `scripts/check-version-sync.sh`
   (which gates all of those in CI) and prints the diff. It commits nothing;
   the changelog content stays human-written, the script only moves it.
   Not bumped by the script and not gated: `PROJECT_NUMBER` in
   `docs/Doxyfile`, edited by hand when someone cares about the Doxygen
   title.
2. Review `git diff`, then commit changelog + fragment deletions + bumps as
   ONE commit: `chore(release): <ver>`.
3. After it lands on `master`, `git tag v<ver> && git push origin v<ver>`.
   A tag matching `v*.*.*` triggers `.github/workflows/release.yml`, which
   builds the app, worker and frontend images, pushes them to
   `ghcr.io/<owner>/<repo>` (and `-worker` / `-frontend`) with the
   **unprefixed** version as the tag, and publishes a GitHub Release seeded
   from auto-generated commit notes. The workflow refuses a tag whose commit
   is not on `master`, and promotes nothing until the CI run of that commit
   (the push run on `master`) has concluded success; tagging right after the
   merge is fine, it waits. A manual `workflow_dispatch` rebuild of a version
   GHCR already carries needs the `force` input. Tags `v*` cannot be moved or
   deleted (repository ruleset).
4. Deploy in the order API → worker → frontend (see `docs/RUNBOOK.md`,
   "Upgrade order").

`vcpkg.json`'s `version` is deliberately NOT part of a release — the
Dockerfile keys the dependency-install layer on that manifest.

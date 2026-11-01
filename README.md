# life-os-cpp

Backend of my Life OS: a C++20 REST service on Drogon with Postgres and Redis.
It carries the account, admin and billing surface of the system plus a fitness
module that pulls Mi Fitness (Xiaomi cloud) health data into Postgres.

[![CI](https://github.com/moveeeax/life-os-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/moveeeax/life-os-cpp/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/moveeeax/life-os-cpp)](https://github.com/moveeeax/life-os-cpp/releases)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)

Images: `ghcr.io/moveeeax/life-os-cpp`, `ghcr.io/moveeeax/life-os-cpp-worker`,
`ghcr.io/moveeeax/life-os-cpp-frontend`. The current version is the newest
heading in [`CHANGELOG.md`](CHANGELOG.md).

## What's inside

- HTTP API on Drogon: JSON error body of one shape, request validators, cache-aside reads through Redis.
- Auth: JWT (HS256) or static bearer, HttpOnly cookie sessions with refresh rotation, RBAC on a permission bitmask, per-user API keys.
- Accounts and admin: register, login, email confirm, password reset, email change, invites, user and role management, audit log (`GET /api/v1/admin/audit`).
- Background jobs: Redis queue, retries (`JOBS_MAX_RETRIES`, optional exponential backoff), dead-letter queue per job type, served by a second binary (`life_os_cpp_worker`).
- Billing: PayPal top-ups into a credit wallet with an append-only ledger, admin packages, settings, payments and metrics.
- Life modules, each behind its own switch (`<NAME>_ENABLED`, default off; a switched-off module answers 404):
  - fitness: Mi Fitness sync into Postgres per linked Xiaomi account, read routes under `/api/v1/fitness/*` (see below);
  - workout: an exercise library, routines and logged sessions, matched with the band's workouts (`/api/v1/workout`);
  - food: products, a meal diary, daily goals from the profile and the scale, Open Food Facts lookup, an LLM parse of typed meals (`/api/v1/food`);
  - money: accounts, a multi-currency ledger, transfers, budgets, reports per currency, daily rates, an LLM parse and a weekly advisor (`/api/v1/money`);
  - tasks: personal tasks with areas and an agenda, an inbox, an LLM parse of a phrase (`/api/v1/tasks`); projects group tasks (`/api/v1/tasks/projects`), and learning shows courses, which are projects of kind course;
  - goals: four kinds of goals with progress and pace, check-ins, sections and milestones (`/api/v1/goals`);
  - now: the Now screen's day digest in one call (`/api/v1/today`), the focus of the day, the wallpaper and focus-sound library (`/api/v1/wallpapers`, `/api/v1/sounds`);
  - travel: border crossings and day counters (Schengen 90/180, tax residency), trips with flights and stays, family documents (`/api/v1/travel`);
  - habits: daily or N-a-week habits ticked by hand or from data, with streaks (`/api/v1/habits`);
  - notes: private Markdown notes with blog-post fields (`/api/v1/notes`);
  - practice: the freelance practice's clients, deals by stage, incomes and expenses, and months with totals per currency (`/api/v1/practice`);
  - github: the user's GitHub repositories, their own and their organisations', pulled by the worker from a linked account, with what needs attention per repository (failing checks, Dependabot alerts, open PRs and issues, missing description, license or README, a year without a push), each rule mutable, and a task from any item (`/api/v1/github`).
- Observability: Prometheus metrics on `:9090`, OpenTelemetry traces over OTLP HTTP, W3C `traceparent` in and `X-Request-Id` out, structured logs.
- React SPA in `frontend/`, built to its own nginx image.
- Docker compose stack in `docker/`, Helm charts in `helm/`, cluster values in `deploy/`.
- OpenAPI 3.1 spec in `docs/openapi.yaml`, drift-checked against the route registry in CI.

## Quick start

Prerequisites: Docker with Compose v2. `make up` pulls the last release
image from `ghcr.io/moveeeax/life-os-cpp:latest`, so nothing compiles and the
stack runs in a minute, but it runs the release, not your checkout. To run
the code you are editing use `make quickstart` (build + up + smoke),
`make up-build` or `make dev`. The first build compiles the vcpkg dependency
set, so give the Docker VM at least 8 GiB of memory (`colima start --cpu 4
--memory 8` on macOS). `make doctor` checks the toolchain, `make warm-cache`
pulls the CI-built dependency layer so the cold build drops from about 30
minutes to a few.

```bash
git clone https://github.com/moveeeax/life-os-cpp.git
cd life-os-cpp

make doctor          # Docker, VM memory, local tools
make up              # last release: app + Postgres + Redis + Mailpit, AUTH_MODE=none
make health          # /healthz, /ready, head of /metrics
make smoke           # curl through the critical endpoints
make logs            # tail the app

make warm-cache      # optional: prebuilt vcpkg layer for the local build
make quickstart      # your checkout: build + up + smoke
```

The API listens on `:8080`, metrics on `:9090`. Migrations from `migrations/`
run on boot (`DB_MIGRATIONS_ENABLED`, default `true`).

Add pieces as needed: `make up-worker` (jobs and DLQ), `make up-monitoring`
(Prometheus on `:9094`, Grafana on `:3000`, Jaeger on `:16686`),
`make frontend-up` (SPA on `:3001`), `make up-everything` (all of it, with
`docker/.env.everything` switching auth to JWT plus cookies). Create the first
admin once the app is up:

```bash
docker compose -f docker/docker-compose.yml exec app \
    ./life_os_cpp --create-admin admin@local password
```

`make down` stops everything, `make down-v` also drops the volumes.

The compose Postgres is `postgres:18-alpine`, the major production runs. A
`postgres_data` volume created before the bump holds 15-format data, which
18 cannot open: run `make down-v` (or `docker volume rm life-os-cpp_postgres_data`)
once before the first `make up` after pulling the change.

Native build without Docker uses the CMake presets in `CMakePresets.json`
(`dev`, `dev-asan`, `release`, `ci`, `coverage`, `devcontainer`):

```bash
make build-local                 # cmake --preset dev, vcpkg toolchain
make test-unit-local             # gtest, no Postgres or Redis needed
make test-local NAME='Jobs*'     # --gtest_filter against the running stack
```

`.devcontainer/` boots from `ghcr.io/moveeeax/life-os-cpp/builder:cache` with
the vcpkg world prebuilt; `docs/TESTING.md` covers that loop.

## Authentication

`AUTH_MODE` is `none` by default, so every route is public on a plain
`make up`. Two other modes:

```bash
# static bearer token (the compose file does not forward AUTH_BEARER_TOKEN, so pass it with -e)
docker compose -f docker/docker-compose.yml run --rm --service-ports \
    -e AUTH_MODE=bearer -e AUTH_BEARER_TOKEN=dev-secret-123 app
curl -H 'Authorization: Bearer dev-secret-123' http://localhost:8080/api/v1/jobs

# JWT HS256 (AUTH_MODE, JWT_SECRET, JWT_ISSUER, JWT_AUDIENCE are forwarded by compose)
AUTH_MODE=jwt JWT_SECRET=change-me JWT_ISSUER=my-issuer JWT_AUDIENCE=my-api make up
TOKEN=$(make jwt SECRET=change-me ROLES=admin)     # scripts/make-jwt.sh, openssl only
curl -H "Authorization: Bearer $TOKEN" http://localhost:8080/api/v1/jobs
```

`make dev-token` stores the minted token in `.dev-token` (gitignored) and
`make smoke` picks it up. The SPA path is `POST /api/v1/auth/register` and
`POST /api/v1/auth/login`, which set HttpOnly access and refresh cookies when
`AUTH_COOKIES_ENABLED=true`.

API keys are per user: `POST /api/v1/account/api-keys` returns a `cpk_`
prefixed secret once; send it in the `X-API-Key` header. A key carries the
permissions of its owner's role, so a machine client needs a user with the
right role and nothing else. Permission bits live in `src/domain/Role.hpp`;
handlers check them, not role names.

## Configuration

Every knob has an env var, a key in `config/config.json` (with
`${VAR:-default}` expansion) and a built-in default, in that priority order.
`CONFIG_FILE` points at another JSON file, for example `config/worker.json`
for the worker or a local overlay such as `config/dev.local.json` (the
`config/*.local.json` pattern and `config/local/` are gitignored).
There is no production JSON profile: production is the chart
`helm/life-os-cpp` plus `deploy/values-prod.yaml`, and `make prod-check`
renders them and asserts the floor on the result (auth on, cookies Secure
with CSRF, rate limit fail-closed, docs off, HSTS on, credentials only from
a Secret). `make env-check` reports unset placeholders of `config/config.json`.

The full table is in [`docs/CONFIG.md`](docs/CONFIG.md).

## Fitness module

Sync of Mi Fitness (Xiaomi cloud) data into Postgres, ported from
[mi-fitness-api](https://github.com/moveeeax/mi-fitness-api). Enabled with
`FITNESS_ENABLED=true`; credentials and sync tuning come through `MI_FITNESS_*`
(`docs/CONFIG.md`, section "Fitness module"). Routes under `/api/v1/fitness/`:

| Route | Permission | What it does |
|---|---|---|
| `GET account`, `POST account/link`, `GET account/link/{id}`, `PATCH account`, `POST account/detect-region`, `DELETE account` | fitness:sync | link your Mi account by QR sign-in, see the link status, set or detect the region, unlink |
| `GET probe` | fitness:sync | live check of your Xiaomi credentials and cloud connectivity |
| `POST sync`, `GET sync/{id}` | fitness:sync | enqueue a sync run of your account, read its journal entry |
| `GET daily-activity`, `sleep`, `heart-rate`, `stress`, `spo2`, `body`, `workouts`, `abnormal-heart-beat` | fitness:read | your rows for a date range |
| `GET summary`, `coverage`, `export` | fitness:read | per-day summary, per-type coverage, json or csv export of your data |

Fitness data belongs to the user whose Mi account it came from. Each user
links their own Xiaomi account (table `mi_accounts`, one account per user);
every route above works on the caller's account, an administrator included,
and a user without a link reads empty lists. The permissions are the bits
`kFitnessRead` (0x04) and `kFitnessSync` (0x08) in `src/domain/Role.hpp` and
mean "own data" and "own sync". The default role `User` has both (migration
`021_fitness_per_account.sql`); `015_fitness_roles.sql` seeds the roles
"Fitness Reader" (0x05) and "Fitness Operator" (0x0D) for API-key users. An
API key inherits the permissions of its user's role and reads that user's
data: an agent needs a key of the user whose data it reads, from
`POST /api/v1/account/api-keys`. Sync runs as a job per account
(`src/fitness/jobs/SyncJob.hpp`, registered in
`src/fitness/jobs/Register.hpp`) on the worker; an optional timer in the
API pod enqueues one for every linked account every
`MI_FITNESS_SYNC_SCHEDULE_HOURS`. The historical design note of the port
(2026-09-29, before the per-user accounts and the MIT license) is in
`docs/fitness/`; this section describes what runs.

## Repo layout

```
src/            api/ (controllers, Endpoints.hpp registry, middleware), core/ (init, Modules.hpp
                switches), database/, cache/, repositories/, domain/, security/, jobs/, llm/,
                net/, storage/, email/, webhooks/, observability/, utils/,
                module code: billing/, fitness/, food/, money/, tasks/, goals/, now/, travel/,
                habits/, notes/, practice/, github/ (workout and projects live in api/ + repositories/),
                main.cpp, worker_main.cpp
tests/          unit/, integration/, api/, e2e/, fitness/, fuzz/
migrations/     NNN_<slug>.sql, applied in order on boot or via --run-migrations
config/         config.json, config.sample.json, worker.json, bench/
docker/         Dockerfile, docker-compose.yml, .env.* presets, Prometheus and Grafana config
helm/           life-os-cpp, life-os-cpp-worker, cpp-frontend, cpp-env umbrella
deploy/         values for the owner's cluster: values-prod.yaml, values-worker-prod.yaml,
                values-frontend-prod.yaml, module ConfigMaps, db/
frontend/       React SPA (Vite, TypeScript, Tailwind, TanStack Query), Dockerfile, nginx.conf
scripts/        make-jwt.sh, smoke.sh, bench.sh, scaffolds (new-*.sh), CI gates (check-*.sh)
docs/           openapi.yaml, CONFIG.md, TESTING.md, RUNBOOK.md, CONVENTIONS.md, SLO.md, ARCHITECTURE.md, fitness/
tools/, third_party/, templates/, changelog.d/
```

## Dev workflow

| Command | What it does |
|---|---|
| `make test` | Full suite in Docker: unit + integration + api, then e2e |
| `make test-unit` / `make test-e2e` | Only the sidecar-free unit bucket / only the wire-level suite |
| `make test-local NAME='Foo*'` | Native gtest run with a filter |
| `make test-watch` | Re-run unit tests on change (watchexec or entr) |
| `make coverage` | gcovr HTML report, fails under `COVERAGE_MIN` |
| `make lint` / `make fmt` | clang-format check / rewrite in place |
| `make tidy` | clang-tidy inside the builder image |
| `make lint-openapi` | spectral over `docs/openapi.yaml` |
| `make ci-local` | format check + drift + spectral + tidy + tests, as CI runs them |
| `make routes` | Print the endpoint table, no DB needed |
| `make migrate` / `make migrate-status` / `make migrate-reset` | Apply / list pending / drop and re-apply |
| `make psql` / `make redis-cli` | Shells against the running stack |
| `make new-endpoint`, `make new-resource`, `make new-job`, `make new-migration` | Scaffolds, see `make help` for arguments |
| `make frontend-dev` / `make frontend-test` / `make frontend-gen-api` | Vite dev server on `:5173` / vitest / regenerate types from the OpenAPI spec |
| `make bench P=baseline E=/api/v1/jobs` | Benchmark preset, methodology in `docs/BENCHMARKS.md` |

Pre-commit hooks (`pre-commit install`) run clang-format, shellcheck, gitleaks,
the OpenAPI drift check and spectral. `CONTRIBUTING.md` has the commit
convention and release flow; `docs/CONVENTIONS.md` is the checklist for adding
a domain entity.

The binary also runs one-shot modes (`./life_os_cpp --help`):
`--print-routes`, `--dump-config` (secrets masked), `--verify-migrations`
(exit 1 while anything is pending), `--run-migrations`, `--setup-dev`
(migrations plus the seeded roles, idempotent), `--create-admin EMAIL [PASS]`
(needs the Administrator role, so `--setup-dev` or a booted app first;
default password `password`) and `--seed-fake [N]` (N fake confirmed users,
default 10, at most 1000, password `password`, for a demo database).

## Kubernetes

Four charts under `helm/`:

- `life-os-cpp`, the HTTP service
- `life-os-cpp-worker`, the job worker
- `cpp-frontend`, the SPA behind rootless nginx
- `cpp-env`, an umbrella that deploys the three as one environment

Production is the owner's arm64 k3s cluster (kube context `oci-k3s`),
namespace `life-os`, as three helm releases. Their values live in `deploy/`:
`values-prod.yaml` (release `life-os-cpp`), `values-worker-prod.yaml`
(`life-os-cpp-worker`) and `values-frontend-prod.yaml`
(`life-os-cpp-frontend`). External CNPG Postgres and Redis, secrets through
`extraEnvFrom`; `deploy/db/` documents the database and role setup,
`deploy/sync-tuning-configmap.yaml` holds the fitness sync knobs. There is
no stage environment. Upgrade the releases in the order below, API, worker,
frontend: the API migrates, and the worker waits in an init container until
the schema of its version is there (`docs/RUNBOOK.md`, "Upgrade order").

```bash
make helm-lint        # helm lint + template over every chart
make helm-validate    # render the umbrella and the deploy/ prod values, assert invariants
helm --kube-context oci-k3s upgrade --install life-os-cpp ./helm/life-os-cpp \
  -n life-os -f deploy/values-prod.yaml
helm --kube-context oci-k3s upgrade --install life-os-cpp-worker ./helm/life-os-cpp-worker \
  -n life-os -f deploy/values-worker-prod.yaml
helm --kube-context oci-k3s upgrade --install life-os-cpp-frontend ./helm/cpp-frontend \
  -n life-os -f deploy/values-frontend-prod.yaml
```

Release images are multi-arch (`linux/amd64` and `linux/arm64`), built
natively per architecture by `release.yml` on `v*` tags. The workflow
refuses a tag that is not on `master` or whose commit has no green CI run,
and promotes the images only after a Trivy scan and cosign signing. Prod
runs only these images. Alerts and what to do when they fire are in
[`docs/RUNBOOK.md`](docs/RUNBOOK.md); the production hardening checklist is in
[`SECURITY.md`](SECURITY.md).

## Documentation

[`docs/INDEX.md`](docs/INDEX.md) is the navigator across every doc and config.
Design decisions are in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) (Drogon,
nlohmann json, module layout, subsystem singletons, SPA split, API versioning).

## License

MIT, see [LICENSE](LICENSE). Third-party dependencies and their licenses are
listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

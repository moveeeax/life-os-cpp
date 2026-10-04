# life-os-cpp

Backend of my Life OS: a C++20 REST service on Drogon with Postgres and Redis.
It carries the account, admin and billing surface of the system plus a fitness
module that pulls Mi Fitness (Xiaomi cloud) health data into Postgres.

[![CI](https://github.com/moveeeax/life-os-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/moveeeax/life-os-cpp/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/moveeeax/life-os-cpp)](https://github.com/moveeeax/life-os-cpp/releases)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)

Current version: 1.7.0. Images: `ghcr.io/moveeeax/life-os-cpp`,
`ghcr.io/moveeeax/life-os-cpp-worker`, `ghcr.io/moveeeax/life-os-cpp-frontend`.

## What's inside

- HTTP API on Drogon: JSON error body of one shape, request validators, cache-aside reads through Redis.
- Auth: JWT (HS256) or static bearer, HttpOnly cookie sessions with refresh rotation, RBAC on a permission bitmask, per-user API keys.
- Accounts and admin: register, login, email confirm, password reset, email change, invites, user and role management, audit log (`GET /api/v1/admin/audit`).
- Background jobs: Redis queue, retries (`JOBS_MAX_RETRIES`, optional exponential backoff), dead-letter queue per job type, served by a second binary (`life_os_cpp_worker`).
- Billing: PayPal top-ups into a credit wallet with an append-only ledger, admin packages, settings, payments and metrics.
- Fitness: Mi Fitness sync into Postgres and read routes under `/api/v1/fitness/*` (see below).
- Observability: Prometheus metrics on `:9090`, OpenTelemetry traces over OTLP HTTP, W3C `traceparent` in and `X-Request-Id` out, structured logs.
- React SPA in `frontend/`, built to its own nginx image.
- Docker compose stack in `docker/`, Helm charts in `helm/`, cluster values in `deploy/`.
- OpenAPI 3.1 spec in `docs/openapi.yaml`, drift-checked against the route registry in CI.

## Quick start

Prerequisites: Docker with Compose v2. The first build compiles the vcpkg
dependency set, so give the Docker VM at least 8 GiB of memory
(`colima start --cpu 4 --memory 8` on macOS). `make doctor` checks the
toolchain, `make warm-cache` pulls the CI-built dependency layer so the cold
build drops from about 30 minutes to a few.

```bash
git clone https://github.com/moveeeax/life-os-cpp.git
cd life-os-cpp

make doctor          # Docker, VM memory, local tools
make warm-cache      # optional: prebuilt vcpkg layer
make up              # app + Postgres + Redis + Mailpit, AUTH_MODE=none
make health          # /healthz, /ready, head of /metrics
make smoke           # curl through the critical endpoints
make logs            # tail the app
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
for the worker or a gitignored `config/local.json` overlay.
`config/config.production.json` is the hardened profile; `make prod-check`
validates it and `make env-check` reports unset placeholders.

The full table is in [`docs/CONFIG.md`](docs/CONFIG.md).

## Fitness module

Sync of Mi Fitness (Xiaomi cloud) data into Postgres, ported from
[mi-fitness-api](https://github.com/moveeeax/mi-fitness-api). Enabled with
`FITNESS_ENABLED=true`; credentials and sync tuning come through `MI_FITNESS_*`
(`docs/CONFIG.md`, section "Fitness module"). Routes under `/api/v1/fitness/`:

| Route | Permission | What it does |
|---|---|---|
| `GET probe` | fitness:sync | live check of the Xiaomi credentials and cloud connectivity |
| `POST sync`, `GET sync/{id}` | fitness:sync | enqueue a sync run, read its journal entry |
| `GET daily-activity`, `sleep`, `heart-rate`, `stress`, `spo2`, `body`, `workouts`, `abnormal-heart-beat` | fitness:read | rows for a date range |
| `GET summary`, `coverage`, `export` | fitness:read | per-day summary, per-type coverage, json or csv export |

The permissions are the bits `kFitnessRead` (0x04) and `kFitnessSync` (0x08)
in `src/domain/Role.hpp`; migration `015_fitness_roles.sql` seeds the roles
"Fitness Reader" (0x05) and "Fitness Operator" (0x0D). An API key inherits the
permissions of its user's role, so an agent needs a user with the Reader role
and a key from `POST /api/v1/account/api-keys`. Sync runs as a job
(`src/jobs/FitnessSyncHandler.hpp`) on the worker; an optional timer in the
API pod enqueues it every `MI_FITNESS_SYNC_SCHEDULE_HOURS`. The port design
note is in `docs/fitness/`.

## Repo layout

```
src/            api/ (controllers, Endpoints.hpp registry, middleware), billing/, cache/, core/,
                database/, domain/, email/, fitness/, jobs/, messaging/, observability/,
                repositories/, security/, tasks/, utils/, webhooks/, main.cpp, worker_main.cpp
tests/          unit/, integration/, api/, e2e/, fitness/, fuzz/
migrations/     NNN_<slug>.sql, applied in order on boot or via --run-migrations
config/         config.json, worker.json, config.production.json, bench/
docker/         Dockerfile, docker-compose.yml, .env.* presets, Prometheus and Grafana config
helm/           life-os-cpp, life-os-cpp-worker, cpp-frontend, cpp-env umbrella
deploy/         values for the owner's cluster: values-prod.yaml, values-worker-prod.yaml, db/
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

The binary also runs one-shot modes: `--print-routes`, `--dump-config`,
`--verify-migrations`, `--run-migrations`, `--create-admin EMAIL [PASS]`.

## Kubernetes

Four charts under `helm/`:

- `life-os-cpp`, the HTTP service
- `life-os-cpp-worker`, the job worker
- `cpp-frontend`, the SPA behind rootless nginx
- `cpp-env`, an umbrella that deploys the three as one environment

`deploy/values-prod.yaml` and `deploy/values-worker-prod.yaml` are the values
for the owner's cluster (external CNPG Postgres and Redis, secrets through
`extraEnvFrom`); `deploy/db/` documents the database and role setup,
`deploy/sync-tuning-configmap.yaml` holds the fitness sync knobs. Each chart
also ships a secret-free `values-prod.example.yaml` to copy from.

```bash
make helm-lint        # helm lint + template over every chart
make helm-validate    # render the cpp-env umbrella and assert deploy invariants
helm upgrade --install api ./helm/life-os-cpp -n life-os -f deploy/values-prod.yaml
```

Release images are `linux/amd64` only, built by `release.yml` on `v*` tags
after a Trivy scan. Alerts and what to do when they fire are in
[`docs/RUNBOOK.md`](docs/RUNBOOK.md); the production hardening checklist is in
[`SECURITY.md`](SECURITY.md).

## Documentation

[`docs/INDEX.md`](docs/INDEX.md) is the navigator across every doc and config.
Design decisions are in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) (Drogon,
nlohmann json, module layout, subsystem singletons, SPA split, API versioning).

## License

MIT, see [LICENSE](LICENSE). Third-party dependencies and their licenses are
listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

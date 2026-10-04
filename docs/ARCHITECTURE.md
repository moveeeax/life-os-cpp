# Architecture

How life-os-cpp is put together and why. This file describes the current
design only. Code comments point here by section number (`docs/ARCHITECTURE.md §4`),
so keep the numbering stable: append new sections, do not renumber.

When a decision changes, rewrite the section in place. History lives in git
and `CHANGELOG.md`.

## 1. System shape

One codebase produces three images from the same git tag:

- `life_os_cpp`, the HTTP API (`src/main.cpp`);
- `life_os_cpp_worker`, the background-job consumer (`src/worker_main.cpp`);
- the React SPA from `frontend/`, served by nginx.

Both binaries link the same static library, `app_core`, and differ only in
their entry point and in the `InitMode` they pass to `Core::initialize()`.
State lives in Postgres (system of record) and Redis (cache, sessions, rate
limits, idempotency keys, the job queue).

Feature modules (billing, fitness) are compiled in always and switched at
runtime: `Core::billing_enabled()` and `Core::fitness_enabled()` in
`src/core/Modules.hpp` read the config, and a disabled module answers 404 from
its handlers. There are no plugins, no `dlopen`, no per-feature deployables.
A single program keeps one init/shutdown lifecycle, whole-program sanitizers
and LTO, and one chart per binary.

Allowed include edges between `src/` directories are declared in
`docs/module-deps.txt` and enforced by `scripts/check-module-deps.sh`. Three
rules are hard-coded in the gate: `utils/` includes nothing outside itself,
`core/Core.hpp` is included only by the entry points (everything else uses
`core/Modules.hpp`), and `webhooks` never includes `email`.

## 2. HTTP layer

The API runs on Drogon: an event-loop server with a controller-class routing
API, TLS and WebSocket support.

- Each controller is an `HttpController<>` whose routes are declared with
  `ADD_METHOD_TO` inside `METHOD_LIST_BEGIN/END`, one line per endpoint.
- Cross-cutting behaviour (request id and tracing, auth, rate limiting,
  idempotency, CORS, metrics) is attached in `src/api/Middleware.cpp` through
  Drogon's advice hooks (`registerSyncAdvice`, `registerPreHandlingAdvice`,
  `registerPostHandlingAdvice`). There is no separate filter framework.

Cost: controllers and middleware are written against Drogon's request and
response types, so replacing the framework means touching every controller
and `src/api/Middleware.cpp`.

Rejected: Crow (weaker middleware story), Pistache (smaller community),
Boost.Beast (no controllers or middleware, everything by hand), cpprestsdk
(unmaintained).

## 3. JSON

Application code uses `nlohmann::json` only.

- Request bodies are parsed with `json::parse(req->body())`.
- Response bodies are built as `nlohmann::json`, serialized with `.dump()` and
  handed to the response as a string.
- `req->getJsonObject()` and any `Json::Value` are off limits: they are
  jsoncpp, which Drogon links for its own use.

Two JSON libraries end up in the binary. That is accepted so that every
handler, error builder and test works with one API. Replacing jsoncpp inside
Drogon would cost more than the duplicate does.

## 4. Module layout

A module is a header plus, when it has heavy bodies, a paired `.cpp`.

Stays in the `.hpp`:

- all template code (`Database::execute_*`, `Config::get<T>`, `Retry::run`,
  `CrudBase`);
- domain structs, repositories and leaf utilities;
- Drogon route macros (`ADD_METHOD_TO`, `METHOD_LIST_*`), because the route
  gates in `scripts/` read controller headers only.

Goes into `src/<dir>/<Module>.cpp`:

- non-template bodies of modules that are expensive to compile or that would
  drag third-party headers (OpenTelemetry SDK, OpenSSL, curl, inja, redis++)
  into every including translation unit.

Every `src/**/*.cpp` except the two entry points is globbed into the
`app_core` STATIC library in `CMakeLists.txt`, so a new module `.cpp` needs no
build-file edit and compiles once for the server, the worker and the tests.

When to move a body out of a header: `scripts/bench-incremental.sh` touches
each hot header and times the warm rebuild. A header whose rebuild takes more
than 30 seconds gets its non-template bodies moved to a `.cpp`. Below that
threshold a module stays header-only, which keeps it to one file.

Rules that follow from headers being included everywhere: helpers go in
anonymous namespaces or are marked `inline`, and a non-`inline` global in a
header is an ODR violation.

## 5. Subsystem lifecycle

Every subsystem is a process-wide singleton behind free functions in its
namespace:

```cpp
namespace MyModule {
void initialize(...);      // throws if already initialized
Impl& get();               // throws if not initialized
bool is_initialized();
void shutdown();
}
```

`Core::initialize()` (`src/core/Core.cpp`) is the only production call site
and fixes the order:

Config, Observability, config validation, Database, Migrations, Cache,
Messaging and Tasks (server only), Security (Auth, RateLimit, Idempotency),
Jobs, Email, Billing.

`InitMode::Worker` skips Messaging and Tasks. `InitMode::MigrateOnly` stops
after Migrations. `Core::shutdown()` tears the same list down in reverse.

Handlers reach a subsystem in one line (`Database::get().execute_read(...)`),
with no constructor injection and no parameter threaded through every
controller method.

Substitution in tests goes through an `install_for_testing` function on the
modules that talk to the outside world: `Database`, `Cache`, `Security::Auth`,
`Billing` (the PayPal client) and the Xiaomi transport in
`src/fitness/xiaomi/Service.hpp`. A new module with external I/O follows the
same shape. Tests that need the real thing call `initialize()` against the
compose Postgres and Redis.

Known limits:

- one instance per process, so two differently configured copies of a
  subsystem cannot coexist;
- a wrong initialization order shows up as an exception at boot, not as a
  compile error.

If per-tenant or per-test instances are ever needed, the singleton becomes
the default instance of a regular class and `get()` returns it.

## 6. Frontend boundary

The backend speaks JSON under `/api/*` and renders no HTML. The only
server-side templates are the email bodies in `templates/email/`.

- The frontend is a Vite + React + TypeScript SPA in `frontend/`, shipped as
  its own nginx image.
- `docs/openapi.yaml` is the contract. `npm run gen:api` generates
  `schema.gen.ts` from it with openapi-typescript, and the hand-written fetch
  wrapper in `frontend/src/lib/api/client.ts` uses those types.
- Sessions are HttpOnly cookies (`__Host-access`, `__Host-refresh`) set by the
  login route. JavaScript never reads them. Each refresh token carries a JTI
  stored in Redis, which makes revocation immediate.
- Everything is same-origin: Vite proxies `/api` in development, nginx proxies
  it to the API in the frontend image. Cookies are `SameSite=Lax`, with CSRF
  protection available through `SECURITY_CSRF_ENABLED`.

Rejected: server-rendered fragments (HTMX), which would tie the UI to the C++
binary; an SSR proxy, which adds a third service; JWTs in `localStorage`,
which any XSS can read.

## 7. API versioning

Every API route is the literal `/api/v<N>/<resource>...`, currently `v1`.

- The version is written out in each route string. There is no shared prefix
  constant, because the route gates extract `(method, path)` pairs with a
  regex and a macro would hide the path from them.
- Probe and infra routes stay unversioned: `/`, `/healthz`, `/ready`,
  `/health`, `/metrics`. Swagger UI and the spec are served at
  `/api/v1/docs` and `/api/v1/openapi.yaml`.
- Inside a major version, changes are additive: new fields, endpoints and
  optional parameters. Nothing is removed, renamed, retyped or tightened.
- A breaking change gets `/api/v2` for the affected routes only, next to v1,
  which keeps running.
- There is no alias from unversioned `/api/...`. All known clients (the SPA
  and API-key consumers of the fitness routes) are deployed by the owner. If
  that stops being true, add the alias as a sync advice registered ahead of
  auth and rate limiting.

A route lives in three places, kept in sync by machine:

| Surface | File | Checked by |
|---|---|---|
| Drogon registration | `src/api/*Controller.hpp` | `scripts/check-routes-registered.sh` |
| Route registry | `src/api/Endpoints.hpp` (`Api::get_endpoints()`) | both scripts |
| Spec | `docs/openapi.yaml` | `scripts/check-openapi-drift.sh` |

`check-routes-registered.sh` also fails on any `/api/` route without a version
segment. `scripts/new-resource.sh` derives paths from one `API_VERSION`
variable, and `scripts/new-endpoint.sh` rejects an unversioned `/api/` path.

The auth and rate-limit allowlists (`public_paths`, `protected_paths` in
`config/config*.json`, defaults in `src/utils/Strings.hpp`) also contain route
paths and are read at runtime. No gate compares them with the registry, so
they are updated by hand when a route moves.

Additive-only is a review rule today. An `oasdiff` gate in CI is the next step
once the API has consumers outside the owner's control.

## 8. What is deliberately absent

These come up in every review. The answer stays no until the stated condition
changes.

Plugins or separately deployable feature modules. See §1: runtime module
switches already give per-feature on/off, and a C++ plugin boundary would add
ABI fragility while giving up whole-program sanitizers and LTO.

A DI container, or passing subsystem references through every handler. See
§5: `install_for_testing` already provides the substitution points the tests
use, and a container would add ceremony on top of them.

A DSL or code generator as the single source of routes. With roughly 75 routes
in 10 controllers, the scaffolder writes the three surfaces from §7 and two
fast gates catch drift. Reconsider if the route count grows several-fold, or
if drift in request and response schemas (which the gates do not check)
becomes a recurring source of bugs.

Registering Drogon routes from `Endpoints.hpp`. The registry holds only
`{method, path, description}` strings. Wiring handlers through it would make
it include every controller, or would replace `HttpController` with lambda
registration everywhere. Same reconsideration conditions as the DSL.

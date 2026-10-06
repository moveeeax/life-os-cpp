# Changelog

All notable changes to this project are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.11.2] - 2026-10-06

### Fixed
- A `food_parse` job that the queue would dead-letter (the provider kept
  timing out or answering 429/5xx) no longer stays `queued`: the last attempt
  marks it `failed` with `provider_unavailable`, and the page says so instead
  of waiting out its deadline. Migration 023 adds `attempts` to
  `food_parse_jobs`.

## [1.11.1] - 2026-10-06

### Fixed
- The `food_parse` job adapts its request to the provider: a 400 that names
  `max_tokens` (OpenAI's reasoning models want `max_completion_tokens`),
  `temperature` (only the default is allowed there) or `response_format` is
  answered by renaming or dropping that parameter and trying again, one
  parameter per round. The provider's message is stored with the error code
  and shown on the page.

## [1.11.0] - 2026-10-06

### Added
- Food module, first part (off by default, `FOOD_ENABLED`): own products, a
  diary of meals per day with copied numbers, daily goals computed from the
  profile and the weight of the Mi scale (Mifflin-St Jeor, activity factor,
  deficit from the weekly pace, overridable per number), day and week views
  with the active kcal of the band, and a proxy to Open Food Facts search and
  product lookup, under `/api/v1/food/`. Every confirmed user has it.
- Food section in the dashboard, for every confirmed user: the day with its
  four meals against the goals (ring and macro bars, the balance with the
  band's active calories), an add form with own and recent products, Open
  Food Facts search, a text description parsed by the LLM job (shown only
  when the server has it configured) and a quick entry; the week with a
  chart and a table; own products with servings and archive; the goals
  profile with the computed numbers and per-target overrides. The sidebar
  item hides while the food module is off.
- Food module, second part: a text description of a meal is parsed by an LLM
  into diary lines. The worker job `food_parse` speaks the OpenAI-compatible
  chat completions API to whatever `FOOD_LLM_BASE_URL`, `FOOD_LLM_API_KEY`,
  `FOOD_LLM_MODEL` and `FOOD_LLM_PROMPT` name; the answer is checked against a
  schema (an item id must be the user's own) and waits in the job for the
  user's confirmation. `POST /api/v1/food/parse`, `GET /api/v1/food/parse/{id}`;
  `GET /api/v1/food/goals` reports `llm_available`.

## [1.10.0] - 2026-10-06

### Added
- Mi account linking per user (backend). A signed-in user links a Xiaomi
  account by QR sign-in through `/api/v1/fitness/account/*`: start an attempt,
  confirm it in a Xiaomi app, and the service verifies the token, finds the
  account's cloud region and stores the link sealed. The link has a status:
  `ok`, or `reauth_required` once Xiaomi refuses the stored token. Credentials
  moved from the single-row `xiaomi_credentials` to `mi_accounts` (one row per
  user; migration 020 gives the existing account to the oldest administrator).
- Profile page: a "Mi Fitness" block to link a Xiaomi account by QR sign-in
  (scan the code in a Xiaomi app, or open the confirmation link on the phone),
  see whether the saved sign-in still works, check it, pick or detect the cloud
  region, and unlink with or without the synced data. Health and Workout show
  "Link your Mi account" to a user without a link instead of empty charts, and
  Health shows a notice when Xiaomi asks to sign in again.

### Changed
- Fitness and Workout data are per user. Every fitness route works on the
  caller's own Mi account: reads return only the rows of that account (a user
  without a link gets empty lists), a sync run belongs to an account and is
  visible only to it, and the schedule enqueues one sync per linked account
  whose token Xiaomi still accepts. Workout readiness, the body-weight snapshot
  and the match with band data read the session owner's account. The default
  role `User` gets `fitness:read` and `fitness:sync` (migration 021), so every
  confirmed user has Health and Workout for their own data. An API key reads
  the data of its own user: a key issued under another user no longer sees the
  owner's data.

### Removed
- The Xiaomi credential seed from the environment: `MI_FITNESS_USER_ID`,
  `MI_FITNESS_PASS_TOKEN`, `MI_FITNESS_REGION` and `MI_FITNESS_RESEED` are no
  longer read. An account is linked from the profile; the region is detected
  per account. `GET /api/v1/fitness/probe` without a linked account answers 409
  `not_linked` instead of 503 `not_configured`.

## [1.9.0] - 2026-10-05

### Added
- Workout module, first part (off by default, `WORKOUT_ENABLED`): an exercise
  library of 876 exercises seeded from the public-domain free-exercise-db,
  custom exercises, and routines with per-exercise targets, under
  `/api/v1/workout/exercises` and `/api/v1/workout/routines`. Access needs
  `fitness:read`.
- Workout module, second part: logged sessions under
  `/api/v1/workout/sessions` (start empty or from a routine, sets with
  client-generated ids so a resend replaces itself, history with volume),
  `/api/v1/workout/readiness`, and matching of finished sessions with Mi Fitness
  heart-rate samples and band workouts. The match runs when a session is
  finished or moved, after every fitness sync, and on
  `POST /api/v1/workout/reconcile` (`fitness:sync`).
- Workout pages in the dashboard, first part: `/workout/exercises` (search and
  filters over the exercise library, photos, own exercises) and
  `/workout/routines` with an editor (weekday, exercises from the library,
  target sets, rep range or duration, rest). The frontend image now carries the
  exercise photos of free-exercise-db under `/exercise-media/` (about 100 MB).
- Workout section in the dashboard sidebar (`/workout`, needs `fitness:read`;
  the item stays inactive while the module is off): start a workout from a
  routine or empty, the active session screen for a phone (sets pre-filled from
  the previous session, rest timer with a sound, add or remove exercises), sets
  that are kept on the device and resent when the connection drops, history
  with volume, and per workout a Health block with heart rate from the band, a
  band workout's calories and a "Sync now" button while the data is awaited.

### Changed
- Prod: the API release no longer has an ingress. The frontend release is the
  only public entry point of `life-os.tarassov.me` and proxies `/api/` to the API
  Service; `/`, `/ready`, `/health` and `/metrics` of the API are reachable only
  inside the cluster.

## [1.8.0] - 2026-10-05

### Added
- Health section of the dashboard (`/health`): tiles (steps, sleep, resting
  heart rate, weight), per-day charts of activity, sleep stages and score, heart
  rate, stress and body composition, tables of SpO2 and workouts, all over the
  existing `/api/v1/fitness/*` routes for a period of 7, 30 or 90 days or a
  custom range kept in the URL. Users with `fitness:sync` also get the coverage
  table, a form that starts a sync run and follows it to completion, and a cloud
  connection check. Charts use ApexCharts.
- Dashboard shell in the frontend, adapted from TailAdmin React: `/` is a
  full-screen sign-in form for a guest and a redirect to `/health` for a signed-in
  user, `/login` redirects to `/`, and `/health` opens inside a sidebar layout
  that lists the Life OS sections (Health is active, the rest are marked "soon").
  The Health page is a placeholder until its charts ship. Admin and account pages
  keep their layout. `deploy/values-frontend-prod.yaml` holds the prod values of
  the frontend chart.

### Changed
- Frontend toolchain upgraded: React 19, Tailwind 4 (Vite plugin, theme in
  `src/index.css`, no `tailwind.config.js` or PostCSS config), Vite 8, vitest 5,
  `react-router` 8 in place of `react-router-dom` 6. The frontend image and the
  CI jobs that install frontend dependencies run on Node 22. No page changes.

## [1.7.2] - 2026-10-04

### Changed
- The ADRs in `docs/adr/` are replaced by one `docs/ARCHITECTURE.md`. Template
  and flask-base wording is removed from docs, comments and names: `GET /` now
  returns `"message": "life-os-cpp"` (was `"C++ API Template"`), and the OpenAPI
  title is `life-os-cpp API`. Migrations 000 and 001 changed in comments only;
  a database that already applied them needs
  `UPDATE schema_migrations SET checksum = NULL WHERE version IN (0, 1)` before
  it boots this version with migrations enabled.

### Fixed
- The API chart did not mount a writable `/app/uploads`, so with the read-only
  root filesystem Drogon logged 256 "Error 30 creating path ./uploads/tmp/NN/"
  lines on every start and had nowhere to spool request bodies. The API
  Deployment now mounts an `emptyDir` there, as the worker chart already did.

## [1.7.1] - 2026-10-04

### Fixed
- Fitness sleep sync stored one night as several rows: the Xiaomi cloud keeps
  each partial upload of a night as its own record, and `sleep_id` was derived
  from the record time. `sleep_id` is now derived from the bedtime, snapshots
  of one night collapse to the one that ends last, and a shorter snapshot no
  longer overwrites a longer stored night. Migration 016 removes the existing
  duplicates and re-keys `sleep_sessions.sleep_id`.

## [1.7.0] - 2026-10-01

### Added
- `fitness` module: Mi Fitness sync ported from mi-fitness-api 1.9.2
  (Xiaomi cloud client, sync, eight health data types, schema 1.0 / CSV
  export) under `/api/v1/fitness/*`, with the `kFitnessRead` / `kFitnessSync`
  permission bits and the Fitness Reader / Fitness Operator roles;
  `FITNESS_ENABLED` master switch.

### Changed
- Repository history reset; template scaffolding, demo overlays and planning
  documents removed.

[Unreleased]: https://github.com/moveeeax/life-os-cpp/compare/v1.7.0...master
[1.7.0]: https://github.com/moveeeax/life-os-cpp/releases/tag/v1.7.0

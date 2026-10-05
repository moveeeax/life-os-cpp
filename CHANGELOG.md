# Changelog

All notable changes to this project are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

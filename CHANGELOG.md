# Changelog

All notable changes to this project are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

# Changelog

All notable changes to this project are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

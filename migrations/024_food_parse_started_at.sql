-- Migration 024: food_parse_jobs.started_at — when the worker last claimed the
-- row. A row left `running` by a crashed worker is claimed again once it is
-- older than the provider timeout, instead of staying `running` for good.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

ALTER TABLE food_parse_jobs ADD COLUMN IF NOT EXISTS started_at timestamptz;

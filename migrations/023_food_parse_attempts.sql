-- Migration 023: food_parse_jobs.attempts — how many times the worker has
-- started a job, so the last attempt of the queue can mark the row failed
-- instead of leaving it `queued` after the queue dead-letters it.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

ALTER TABLE food_parse_jobs ADD COLUMN IF NOT EXISTS attempts integer NOT NULL DEFAULT 0;

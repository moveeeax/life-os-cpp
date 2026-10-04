-- Migration 013: fitness_sync_run_mutex
-- Created: 2026-09-30T03:08:27Z
--
-- Migrations are applied in numeric order on app boot (or via
-- RUN_MIGRATIONS_ONLY=1 ./life_os_cpp). Use --verify-migrations to
-- list pending without applying.
--
-- The MigrationRunner already wraps this file in ONE transaction (under an
-- advisory lock) together with the schema_migrations bookkeeping. Do NOT add
-- BEGIN/COMMIT — an embedded COMMIT ends that transaction early and breaks
-- atomicity. Prefer idempotent DDL (IF NOT EXISTS / ON CONFLICT DO NOTHING).

-- TODO: write your DDL here. Common table skeleton (uncomment + rename):
--
-- CREATE TABLE IF NOT EXISTS your_table (
--     id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
--     name       TEXT        NOT NULL,
--     created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
--     updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
-- );
--
-- DROP TRIGGER IF EXISTS your_table_touch_updated_at ON your_table;
-- CREATE TRIGGER your_table_touch_updated_at
--     BEFORE UPDATE ON your_table
--     FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- Sync mutual exclusion: a single running row for the whole system.
-- An advisory lock does not fit: it is session-scoped, and the connection pool
-- hands out a different session on every call. The partial unique index makes
-- the queued -> running transition atomic; a second concurrent run hits the
-- uniqueness violation and finishes honestly with status skipped.
CREATE UNIQUE INDEX IF NOT EXISTS one_running_sync_run
    ON sync_runs ((1))
    WHERE status = 'running';

-- Migration 044: the user's quick links on Now (the Links corner).
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

-- [{"title","url"}], at most 8, checked by the API; null column = no links.
ALTER TABLE now_settings ADD COLUMN IF NOT EXISTS links jsonb;

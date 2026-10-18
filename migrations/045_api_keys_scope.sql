-- Migration 045: a scope per API key, so a machine client can be given a key
-- that reads but never writes.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

-- 'full' is every key minted before this migration: reads and writes the
-- owner's data. 'read' answers only GET, HEAD and OPTIONS (the middleware
-- refuses the rest with 403 key_read_only).
ALTER TABLE api_keys ADD COLUMN IF NOT EXISTS scope text NOT NULL DEFAULT 'full';
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'api_keys_scope_check') THEN
        ALTER TABLE api_keys ADD CONSTRAINT api_keys_scope_check CHECK (scope IN ('full', 'read'));
    END IF;
END $$;

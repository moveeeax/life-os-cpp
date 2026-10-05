-- Migration 021: fitness_per_account — sync bookkeeping per Xiaomi account,
-- and fitness access for every user.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- Migration 020 added xiaomi_user_id to sync_runs and sync_state and filled it
-- for the existing account. Here it becomes part of the keys: each account has
-- its own sync state and its own "one running run".

-- State without an account cannot be attributed (an installation that never
-- had a link); runs keep their history under the empty account.
DELETE FROM sync_state WHERE xiaomi_user_id IS NULL;
UPDATE sync_runs SET xiaomi_user_id = '' WHERE xiaomi_user_id IS NULL;
ALTER TABLE sync_runs  ALTER COLUMN xiaomi_user_id SET DEFAULT '';
ALTER TABLE sync_runs  ALTER COLUMN xiaomi_user_id SET NOT NULL;
ALTER TABLE sync_state ALTER COLUMN xiaomi_user_id SET NOT NULL;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_index i JOIN pg_class c ON c.oid = i.indrelid
        WHERE c.relname = 'sync_state' AND i.indisprimary AND i.indnatts = 2) THEN
        ALTER TABLE sync_state DROP CONSTRAINT IF EXISTS sync_state_pkey;
        ALTER TABLE sync_state ADD PRIMARY KEY (xiaomi_user_id, data_type);
    END IF;
END $$;

-- One running run per account instead of one per system.
DROP INDEX IF EXISTS one_running_sync_run;
CREATE UNIQUE INDEX IF NOT EXISTS one_running_sync_run_per_account
    ON sync_runs (xiaomi_user_id) WHERE status = 'running';
CREATE INDEX IF NOT EXISTS idx_sync_runs_account ON sync_runs (xiaomi_user_id, id DESC);

-- Every user reads and syncs their own fitness data: the default role gets
-- FITNESS_READ (0x04) and FITNESS_SYNC (0x08). The bits now mean "own data".
UPDATE roles SET permissions = permissions | 12 WHERE is_default;

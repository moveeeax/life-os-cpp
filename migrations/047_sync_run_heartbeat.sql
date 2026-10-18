-- Migration 047: a heartbeat on a running sync run.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

-- The worker refreshes heartbeat_at while a run talks to Xiaomi (at most every
-- 30 seconds). A `running` row whose heartbeat has gone quiet was left by a
-- worker that is gone: a redelivery of the same job takes it over, and a run
-- of the same account no longer waits hours for the stale sweep. NULL is a run
-- started by a binary before this migration; it keeps the old rule (stale
-- after the per-type ceiling times the number of types).
ALTER TABLE sync_runs ADD COLUMN IF NOT EXISTS heartbeat_at timestamptz;

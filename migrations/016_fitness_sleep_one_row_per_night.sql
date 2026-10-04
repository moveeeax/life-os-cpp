-- Migration 016: fitness_sleep_one_row_per_night — collapse sleep snapshots.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent.
--
-- The cloud keeps every partial upload of a night as its own record: same
-- bedtime, later wake-up time. sleep_id used to end with the record time, so
-- each snapshot became a row. sleep_id now ends with the bedtime epoch
-- (src/fitness/xiaomi/Normalize.cpp).
--
-- 1. Per (user, source prefix, bedtime) keep the snapshot that ends last.
DELETE FROM sleep_sessions s
USING sleep_sessions longer
WHERE longer.user_id = s.user_id
  AND longer.start_at = s.start_at
  AND regexp_replace(longer.sleep_id, '_[^_]*$', '') = regexp_replace(s.sleep_id, '_[^_]*$', '')
  AND (longer.end_at > s.end_at OR (longer.end_at = s.end_at AND longer.id > s.id));

-- 2. Re-key the survivors by bedtime. A second run rewrites nothing.
UPDATE sleep_sessions
SET sleep_id = regexp_replace(sleep_id, '_[^_]*$', '') || '_' || extract(epoch FROM start_at)::bigint
WHERE sleep_id <> regexp_replace(sleep_id, '_[^_]*$', '') || '_' || extract(epoch FROM start_at)::bigint;

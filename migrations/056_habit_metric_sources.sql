-- Migration 056: a habit reads any series of the metrics catalog
-- (src/metrics/Catalog.hpp): its source is 'manual' or a catalog id such as
-- 'fitness:steps', 'food:kcal' or 'habits:<uuid>', compared to the goal at
-- least (the default) or at most. The goal rules per source move to the
-- controller, which knows the catalog: the Mi goal rule (habits_check1, the
-- unnamed CHECK of migration 037) and the weigh-in rule of migration 055 go.
-- The five sources stored before the catalog get their catalog ids; a
-- workout's goal, which 1.33 dropped on write and ignored on read, is cleared
-- so the count of sessions keeps meaning "any session".
--
-- Expand and contract: the CHECK still takes the five old names, so the
-- previous binary can write them while it serves during the rollout and after
-- a rollback (the new binary reads them as their catalog ids). A later
-- release may tighten it.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

ALTER TABLE habits DROP CONSTRAINT IF EXISTS habits_source_check;
ALTER TABLE habits DROP CONSTRAINT IF EXISTS habits_check1;
ALTER TABLE habits DROP CONSTRAINT IF EXISTS habits_weight_goal_check;

UPDATE habits SET source = 'fitness:sleep' WHERE source = 'sleep';
UPDATE habits SET source = 'fitness:steps' WHERE source = 'steps';
UPDATE habits SET source = 'fitness:active_kcal' WHERE source = 'active_kcal';
UPDATE habits SET source = 'workout:sessions', goal = NULL WHERE source = 'workout';
UPDATE habits SET source = 'fitness:weight' WHERE source = 'weight';

ALTER TABLE habits ADD CONSTRAINT habits_source_check
    CHECK (source = 'manual' OR source LIKE '%:%'
           OR source IN ('sleep', 'steps', 'active_kcal', 'workout', 'weight'));

ALTER TABLE habits ADD COLUMN IF NOT EXISTS direction text NOT NULL DEFAULT 'at_least'
    CHECK (direction IN ('at_least', 'at_most'));

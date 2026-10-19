-- Migration 055: the habit source 'weight', a day ticked by a weigh-in on the
-- Mi scales (a body_measurements row that local day). Like 'workout' it has no
-- goal: any weighing ticks the day, so a goal is refused here as well as in
-- the controller. The goal rule of the Mi sources (habits_check1, migration
-- 037) stays as it is and does not name 'weight'.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

-- habits_source_check is the name Postgres gave the inline CHECK of migration 037.
ALTER TABLE habits DROP CONSTRAINT IF EXISTS habits_source_check;
ALTER TABLE habits ADD CONSTRAINT habits_source_check
    CHECK (source IN ('manual', 'sleep', 'steps', 'active_kcal', 'workout', 'weight'));

ALTER TABLE habits DROP CONSTRAINT IF EXISTS habits_weight_goal_check;
ALTER TABLE habits ADD CONSTRAINT habits_weight_goal_check
    CHECK (source <> 'weight' OR goal IS NULL);

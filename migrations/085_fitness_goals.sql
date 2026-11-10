-- Migration 085: the caller's own fitness goals, for now the daily steps goal
-- (GET/PUT /api/v1/fitness/goals; 8000 steps when unset). Keyed by the owner,
-- not by the Mi account, so the goal stays when the band is unlinked.
--
-- The runner wraps each migration in its own transaction under an advisory
-- lock: no BEGIN/COMMIT here. Idempotent DDL.

CREATE TABLE IF NOT EXISTS fitness_goals (
    owner_id   uuid        PRIMARY KEY,
    steps      integer     CHECK (steps BETWEEN 1000 AND 100000),
    updated_at timestamptz NOT NULL DEFAULT now()
);

DROP TRIGGER IF EXISTS fitness_goals_touch_updated_at ON fitness_goals;
CREATE TRIGGER fitness_goals_touch_updated_at BEFORE UPDATE ON fitness_goals
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- Migration 086: Ani's level (GET /api/v1/ani/level). The points are read from
-- the person's own records day by day (tasks, food, band, money), so only the
-- day the count starts from is kept: set on the first read, 90 days back, and
-- never moved, so the level does not slide when old days leave a window.
--
-- The runner wraps each migration in its own transaction under an advisory
-- lock: no BEGIN/COMMIT here. Idempotent DDL.

CREATE TABLE IF NOT EXISTS ani_level_start (
    owner_id   uuid        PRIMARY KEY,
    started_on date        NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now()
);

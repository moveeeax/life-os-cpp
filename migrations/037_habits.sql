-- Migration 037: habits — the owner's habits (every day, or N times a week,
-- with an optional goal and a source of data that ticks them) and the manual
-- ticks. Values from Mi Fitness and Training are computed on read, never
-- stored here.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS habits (
    id           uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id     uuid        NOT NULL,
    name         text        NOT NULL CHECK (length(name) BETWEEN 1 AND 80),
    schedule     text        NOT NULL CHECK (schedule IN ('daily', 'weekly')),
    per_week     integer     CHECK (per_week BETWEEN 1 AND 7),
    goal         numeric     CHECK (goal > 0),
    unit         text        NOT NULL DEFAULT '' CHECK (length(unit) <= 16),
    source       text        NOT NULL DEFAULT 'manual'
                             CHECK (source IN ('manual', 'sleep', 'steps', 'active_kcal', 'workout')),
    position     integer     NOT NULL DEFAULT 0,
    archived_at  timestamptz,
    created_at   timestamptz NOT NULL DEFAULT now(),
    -- A weekly habit says how many times; a daily one does not.
    CHECK ((schedule = 'weekly') = (per_week IS NOT NULL)),
    -- Sleep (hours), steps and active kcal are met against a goal.
    CHECK (source NOT IN ('sleep', 'steps', 'active_kcal') OR goal IS NOT NULL)
);
CREATE INDEX IF NOT EXISTS habits_owner_position ON habits (owner_id, position);

-- A manual tick of a habit on a day; a deleted habit takes its ticks along.
CREATE TABLE IF NOT EXISTS habit_checks (
    owner_id    uuid        NOT NULL,
    habit_id    uuid        NOT NULL REFERENCES habits (id) ON DELETE CASCADE,
    date        date        NOT NULL,
    value       numeric,
    created_at  timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (habit_id, date)
);
CREATE INDEX IF NOT EXISTS habit_checks_owner_date ON habit_checks (owner_id, date);

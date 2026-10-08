-- Migration 030: goals — the person's goals of four kinds, the check-ins of a
-- number goal, the sections of a steps goal, the milestones of a binary
-- goal; tasks may belong to a goal and to one of its sections, and gain the
-- in_progress status.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS goal_items (
    id            uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id      uuid        NOT NULL,
    title         text        NOT NULL CHECK (length(title) BETWEEN 1 AND 200),
    area          text        NOT NULL CHECK (area IN ('finance', 'health', 'travel', 'growth', 'relationships', 'projects')),
    kind          text        NOT NULL CHECK (kind IN ('number', 'steps', 'count', 'binary')),
    why           text        NOT NULL DEFAULT '' CHECK (length(why) <= 300),
    start_date    date        NOT NULL,
    due           date        NOT NULL,
    status        text        NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'done', 'dropped')),
    completed_at  timestamptz,
    unit          text        NOT NULL DEFAULT '' CHECK (length(unit) <= 20),
    start_value   numeric(20,4),
    target_value  numeric(20,4),
    target_count  integer     CHECK (target_count BETWEEN 1 AND 10000),
    result        text        CHECK (result IN ('pass', 'fail')),
    created_at    timestamptz NOT NULL DEFAULT now(),
    updated_at    timestamptz NOT NULL DEFAULT now(),
    CHECK (due > start_date),
    CHECK ((status = 'done') = (completed_at IS NOT NULL)),
    CHECK ((kind = 'number') = (start_value IS NOT NULL AND target_value IS NOT NULL)),
    CHECK (kind <> 'number' OR start_value <> target_value),
    CHECK ((kind = 'count') = (target_count IS NOT NULL)),
    CHECK (kind = 'binary' OR result IS NULL),
    CHECK (kind = 'number' OR unit = '')
);
CREATE INDEX IF NOT EXISTS idx_goal_items_owner ON goal_items (owner_id, status, due);

CREATE TABLE IF NOT EXISTS goal_checkins (
    id          uuid          PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid          NOT NULL,
    goal_id     uuid          NOT NULL REFERENCES goal_items (id) ON DELETE CASCADE,
    date        date          NOT NULL,
    value       numeric(20,4) NOT NULL,
    note        text          NOT NULL DEFAULT '' CHECK (length(note) <= 200),
    created_at  timestamptz   NOT NULL DEFAULT now(),
    UNIQUE (goal_id, date)
);

CREATE TABLE IF NOT EXISTS goal_sections (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    goal_id     uuid        NOT NULL REFERENCES goal_items (id) ON DELETE CASCADE,
    name        text        NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
    position    integer     NOT NULL DEFAULT 0,
    created_at  timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_goal_sections_goal ON goal_sections (goal_id, position);

CREATE TABLE IF NOT EXISTS goal_milestones (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    goal_id     uuid        NOT NULL REFERENCES goal_items (id) ON DELETE CASCADE,
    date        date        NOT NULL,
    label       text        NOT NULL CHECK (length(label) BETWEEN 1 AND 120),
    created_at  timestamptz NOT NULL DEFAULT now()
);

ALTER TABLE task_items ADD COLUMN IF NOT EXISTS goal_id uuid REFERENCES goal_items (id) ON DELETE SET NULL;
ALTER TABLE task_items ADD COLUMN IF NOT EXISTS goal_section_id uuid REFERENCES goal_sections (id) ON DELETE SET NULL;
CREATE INDEX IF NOT EXISTS idx_task_items_goal ON task_items (goal_id) WHERE goal_id IS NOT NULL;
ALTER TABLE task_items DROP CONSTRAINT IF EXISTS task_items_status_check;
ALTER TABLE task_items ADD CONSTRAINT task_items_status_check CHECK (status IN ('open', 'in_progress', 'done'));

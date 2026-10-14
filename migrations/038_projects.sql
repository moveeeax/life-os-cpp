-- Migration 038: projects — a stream of work that groups tasks — and courses of
-- Learning, which are projects of kind 'course' with a platform and dates.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS projects (
    id           uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id     uuid        NOT NULL,
    kind         text        NOT NULL DEFAULT 'project' CHECK (kind IN ('project', 'course')),
    name         text        NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
    -- The handle a task phrase uses: "write the runbook #small".
    code         text        NOT NULL CHECK (code ~ '^[a-z0-9-]{1,24}$'),
    description  text        NOT NULL DEFAULT '' CHECK (length(description) <= 500),
    link         text        NOT NULL DEFAULT '' CHECK (length(link) <= 500),
    status       text        NOT NULL CHECK (status IN ('planned', 'active', 'paused', 'done')),
    platform     text        NOT NULL DEFAULT '' CHECK (length(platform) <= 80),
    starts_on    date,
    ends_on      date,
    certificate  text        NOT NULL DEFAULT '' CHECK (length(certificate) <= 500),
    position     integer     NOT NULL DEFAULT 0,
    created_at   timestamptz NOT NULL DEFAULT now(),
    updated_at   timestamptz NOT NULL DEFAULT now(),
    -- A project is active, paused or done; a course is planned, active or done.
    CHECK ((kind = 'project' AND status IN ('active', 'paused', 'done'))
        OR (kind = 'course' AND status IN ('planned', 'active', 'done'))),
    CHECK (ends_on IS NULL OR starts_on IS NULL OR ends_on >= starts_on)
);
CREATE UNIQUE INDEX IF NOT EXISTS projects_owner_code ON projects (owner_id, code);
CREATE INDEX IF NOT EXISTS projects_owner_kind ON projects (owner_id, kind, status);

ALTER TABLE task_items ADD COLUMN IF NOT EXISTS project_id uuid REFERENCES projects (id) ON DELETE SET NULL;
CREATE INDEX IF NOT EXISTS task_items_project ON task_items (project_id) WHERE project_id IS NOT NULL;

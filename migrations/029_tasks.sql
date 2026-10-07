-- Migration 029: tasks — the person's own tasks, the inbox of notes that are
-- not tasks yet, and the tasks_parse jobs that turn a phrase into a draft.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- Work is not here: tasks of the job live in Linear. Areas and efforts are
-- fixed lists (docs/superpowers/specs/2026-10-07-tasks-section-design.md §3).

CREATE TABLE IF NOT EXISTS task_items (
    id            uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id      uuid        NOT NULL,
    title         text        NOT NULL CHECK (length(title) BETWEEN 1 AND 200),
    area          text        NOT NULL CHECK (area IN ('finance', 'health', 'travel', 'growth', 'relationships', 'projects')),
    effort        text        CHECK (effort IN ('5min', '30min', 'deep')),
    due           date,
    next_step     text        NOT NULL DEFAULT '' CHECK (length(next_step) <= 300),
    note          text        NOT NULL DEFAULT '' CHECK (length(note) <= 2000),
    status        text        NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'done')),
    completed_at  timestamptz,
    source_kind   text        CHECK (source_kind IN ('url', 'money_transaction')),
    source_ref    text        CHECK (length(source_ref) <= 2000),
    external_id   text        CHECK (length(external_id) <= 100),
    created_at    timestamptz NOT NULL DEFAULT now(),
    updated_at    timestamptz NOT NULL DEFAULT now(),
    CHECK ((status = 'done') = (completed_at IS NOT NULL)),
    CHECK ((source_kind IS NULL) = (source_ref IS NULL))
);
CREATE INDEX IF NOT EXISTS idx_task_items_owner_open ON task_items (owner_id, status, due);
CREATE UNIQUE INDEX IF NOT EXISTS uq_task_items_external ON task_items (owner_id, external_id) WHERE external_id IS NOT NULL;

CREATE TABLE IF NOT EXISTS task_notes (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    text        text        NOT NULL CHECK (length(text) BETWEEN 1 AND 1000),
    status      text        NOT NULL DEFAULT 'inbox' CHECK (status IN ('inbox', 'archived')),
    task_id     uuid        REFERENCES task_items (id) ON DELETE SET NULL,
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_task_notes_owner ON task_notes (owner_id, status, created_at DESC);

CREATE TABLE IF NOT EXISTS task_parse_jobs (
    id                 uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id           uuid        NOT NULL,
    text               text        NOT NULL CHECK (length(text) BETWEEN 1 AND 2000),
    hint_date          date        NOT NULL,
    note_id            uuid        REFERENCES task_notes (id) ON DELETE SET NULL,
    status             text        NOT NULL DEFAULT 'queued' CHECK (status IN ('queued', 'running', 'done', 'failed')),
    attempts           integer     NOT NULL DEFAULT 0,
    started_at         timestamptz,
    result             jsonb,
    error              text,
    model              text,
    prompt_tokens      integer,
    completion_tokens  integer,
    accepted_at        timestamptz,
    created_at         timestamptz NOT NULL DEFAULT now(),
    finished_at        timestamptz
);
CREATE INDEX IF NOT EXISTS idx_task_parse_jobs_owner ON task_parse_jobs (owner_id, created_at DESC);

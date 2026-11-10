-- Migration 083: the parse that applies its own rows, and the one parse for
-- every kind of a phrase.
--
-- money_parse_jobs and task_parse_jobs learn `auto_apply` (the worker posts
-- the lines itself when the job ends, instead of waiting for an accept),
-- `applied` (what that created: row ids per kind, and the lines it could not
-- place) and `idempotency_key` (the request's Idempotency-Key: a retried POST
-- finds its job instead of queueing a second one, with or without the Redis
-- middleware). money_parse_jobs also keeps the `account_id` the request named.
--
-- parse_jobs is the unified parse (POST /api/v1/parse): one phrase, one model
-- call, the rows per kind in `result`; when it ends the worker files each
-- kind's lines as a finished child row of that module's job table, so the
-- module's own accept route takes them.
--
-- Expand only: the new columns have defaults or are nullable, so the previous
-- binary keeps inserting and reading these tables as before.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

ALTER TABLE money_parse_jobs ADD COLUMN IF NOT EXISTS auto_apply boolean NOT NULL DEFAULT false;
ALTER TABLE money_parse_jobs ADD COLUMN IF NOT EXISTS account_id uuid;
ALTER TABLE money_parse_jobs ADD COLUMN IF NOT EXISTS applied jsonb;
ALTER TABLE money_parse_jobs ADD COLUMN IF NOT EXISTS idempotency_key text
    CHECK (idempotency_key IS NULL OR length(idempotency_key) BETWEEN 1 AND 200);
CREATE UNIQUE INDEX IF NOT EXISTS uq_money_parse_jobs_idempotency
    ON money_parse_jobs (owner_id, idempotency_key) WHERE idempotency_key IS NOT NULL;

ALTER TABLE task_parse_jobs ADD COLUMN IF NOT EXISTS auto_apply boolean NOT NULL DEFAULT false;
ALTER TABLE task_parse_jobs ADD COLUMN IF NOT EXISTS applied jsonb;
ALTER TABLE task_parse_jobs ADD COLUMN IF NOT EXISTS idempotency_key text
    CHECK (idempotency_key IS NULL OR length(idempotency_key) BETWEEN 1 AND 200);
CREATE UNIQUE INDEX IF NOT EXISTS uq_task_parse_jobs_idempotency
    ON task_parse_jobs (owner_id, idempotency_key) WHERE idempotency_key IS NOT NULL;

CREATE TABLE IF NOT EXISTS parse_jobs (
    id                 uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id           uuid        NOT NULL,
    text               text        NOT NULL CHECK (length(text) BETWEEN 1 AND 2000),
    meal               text        NOT NULL CHECK (meal IN ('breakfast', 'lunch', 'dinner', 'snack')),
    date               date        NOT NULL,
    auto_apply         boolean     NOT NULL DEFAULT false,
    account_id         uuid,
    idempotency_key    text        CHECK (idempotency_key IS NULL OR length(idempotency_key) BETWEEN 1 AND 200),
    status             text        NOT NULL DEFAULT 'queued' CHECK (status IN ('queued', 'running', 'done', 'failed')),
    attempts           integer     NOT NULL DEFAULT 0,
    started_at         timestamptz,
    result             jsonb,
    applied            jsonb,
    error              text,
    model              text,
    prompt_tokens      integer,
    completion_tokens  integer,
    created_at         timestamptz NOT NULL DEFAULT now(),
    finished_at        timestamptz
);
CREATE INDEX IF NOT EXISTS idx_parse_jobs_owner ON parse_jobs (owner_id, created_at DESC);
CREATE UNIQUE INDEX IF NOT EXISTS uq_parse_jobs_idempotency
    ON parse_jobs (owner_id, idempotency_key) WHERE idempotency_key IS NOT NULL;

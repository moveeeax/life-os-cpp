-- Migration 026: money_parse_jobs — a bank mail, a list or a receipt photo
-- sent to the LLM by the money_parse worker job; the lines wait here until
-- the user accepts them into the inbox as pending rows.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- The image is kept only while the job runs: every terminal state sets it to null.

CREATE TABLE IF NOT EXISTS money_parse_jobs (
    id                 uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id           uuid        NOT NULL,
    kind               text        NOT NULL CHECK (kind IN ('text', 'receipt')),
    text               text        NOT NULL DEFAULT '' CHECK (length(text) <= 8000),
    image              bytea,
    image_type         text        CHECK (image_type IN ('image/jpeg', 'image/png', 'image/webp')),
    hint_date          date        NOT NULL,
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
    finished_at        timestamptz,
    CHECK (kind = 'receipt' OR length(text) >= 1),
    CHECK ((kind = 'receipt') = (image_type IS NOT NULL))
);
CREATE INDEX IF NOT EXISTS idx_money_parse_jobs_owner ON money_parse_jobs (owner_id, created_at DESC);

-- Migration 027: money_advisor_reports — the advisor's written reviews, each
-- stored with the facts it was built on (the same numbers the Reports page
-- shows), so every figure in the text can be checked.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- One review per owner, period kind and period start: a second scheduler tick
-- (or a second API pod) finds the row and enqueues nothing.

CREATE TABLE IF NOT EXISTS money_advisor_reports (
    id                 uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id           uuid        NOT NULL,
    period             text        NOT NULL CHECK (period IN ('week', 'month', 'quarter')),
    period_start       date        NOT NULL,
    period_end         date        NOT NULL,
    status             text        NOT NULL DEFAULT 'queued' CHECK (status IN ('queued', 'running', 'done', 'failed')),
    attempts           integer     NOT NULL DEFAULT 0,
    started_at         timestamptz,
    facts              jsonb,
    content            text        CHECK (length(content) <= 20000),
    error              text,
    model              text,
    prompt_tokens      integer,
    completion_tokens  integer,
    created_at         timestamptz NOT NULL DEFAULT now(),
    finished_at        timestamptz,
    CHECK (period_end >= period_start),
    UNIQUE (owner_id, period, period_start)
);
CREATE INDEX IF NOT EXISTS idx_money_advisor_reports_owner ON money_advisor_reports (owner_id, period_start DESC);

-- Migration 059: the chat with Ani's agent (src/agent/Turn.hpp). One row per
-- message the user sent and the reply it got: the panel on Now reads these,
-- while the agent's own memory of the talk lives in its pod's snapshot.
-- A user has at most one turn queued or running; the worker runs it in the
-- job ani_turn and leaves it done or failed.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS ani_turns (
    id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid NOT NULL,
    status      text NOT NULL DEFAULT 'queued'
                CHECK (status IN ('queued', 'running', 'done', 'failed')),
    text        text NOT NULL CHECK (char_length(text) BETWEEN 1 AND 4000),
    -- The user's zone when they wrote: the tools read their day in it.
    tz          text NOT NULL DEFAULT 'UTC',
    reply       text NULL,
    -- Why a failed turn failed, as a short code the panel words
    -- (busy, budget, timeout, unavailable, failed).
    error       text NULL,
    -- The names of the tools the turn ran, in order.
    tools       jsonb NOT NULL DEFAULT '[]'::jsonb,
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS ani_turns_owner_created ON ani_turns (owner_id, created_at DESC);
CREATE UNIQUE INDEX IF NOT EXISTS ani_turns_one_open
    ON ani_turns (owner_id) WHERE status IN ('queued', 'running');

DROP TRIGGER IF EXISTS ani_turns_touch_updated_at ON ani_turns;
CREATE TRIGGER ani_turns_touch_updated_at BEFORE UPDATE ON ani_turns
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

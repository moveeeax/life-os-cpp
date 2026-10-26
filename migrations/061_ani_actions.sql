-- Migration 061: changes Ani's agent proposes in a chat turn (src/agent/Actions.hpp).
-- The agent's write tools do not write: each call becomes a pending action
-- here, shown under her reply with Confirm and Cancel. A confirmed action is
-- run by the job ani_action as the user, against the site's API, and ends
-- done or failed. A pending action older than an hour can no longer be
-- confirmed (the view reads it as expired). Deleting the turn deletes its actions.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS ani_actions (
    id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid NOT NULL,
    turn_id     uuid NOT NULL REFERENCES ani_turns (id) ON DELETE CASCADE,
    kind        text NOT NULL
                CHECK (kind IN ('create_task', 'complete_task', 'check_habit', 'add_note')),
    -- What the person confirms, in words ("Add the task «Milk», due 2026-10-27").
    summary     text NOT NULL CHECK (char_length(summary) BETWEEN 1 AND 600),
    -- The API request it runs: {method, path, query: [[k, v]...], body}.
    request     jsonb NOT NULL,
    status      text NOT NULL DEFAULT 'pending'
                CHECK (status IN ('pending', 'confirmed', 'running', 'done', 'failed', 'cancelled')),
    -- Why a failed action failed: the API's error code, or a short one of ours.
    error       text NULL,
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS ani_actions_turn ON ani_actions (turn_id, created_at);
CREATE INDEX IF NOT EXISTS ani_actions_owner ON ani_actions (owner_id);

DROP TRIGGER IF EXISTS ani_actions_touch_updated_at ON ani_actions;
CREATE TRIGGER ani_actions_touch_updated_at BEFORE UPDATE ON ani_actions
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

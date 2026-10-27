-- Migration 062: Ani's personal webhook and the bare outfit switch.
-- ani_prefs: per-user Ani settings kept on the server (the bare outfit is on
-- when bare_on_at is set; the user confirmed they are an adult then).
-- ani_hooks: one secret webhook token per user (src/api/AniHookController).
-- ani_inbox: what came through the webhook, waiting for the Now page: say,
-- outfit and act items are ready at once; an event goes to the agent first
-- (thinking, then ready with her line, or delivered silent, or failed).
-- ani_turns.origin: a turn the user wrote, or one the webhook made; each
-- origin has at most one open turn, so a user message is not refused while
-- a webhook turn runs (the worker runs them one after the other).
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS ani_prefs (
    owner_id    uuid PRIMARY KEY,
    bare_on_at  timestamptz NULL,
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now()
);

DROP TRIGGER IF EXISTS ani_prefs_touch_updated_at ON ani_prefs;
CREATE TRIGGER ani_prefs_touch_updated_at BEFORE UPDATE ON ani_prefs
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE TABLE IF NOT EXISTS ani_hooks (
    owner_id      uuid PRIMARY KEY,
    -- aniwh_ + 48 hex; kept as is so the settings page can show the URL.
    token         text NOT NULL UNIQUE CHECK (token ~ '^aniwh_[0-9a-f]{48}$'),
    created_at    timestamptz NOT NULL DEFAULT now(),
    rotated_at    timestamptz NULL,
    last_used_at  timestamptz NULL
);

ALTER TABLE ani_turns ADD COLUMN IF NOT EXISTS origin text NOT NULL DEFAULT 'user';
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'ani_turns_origin_check') THEN
        ALTER TABLE ani_turns ADD CONSTRAINT ani_turns_origin_check CHECK (origin IN ('user', 'hook'));
    END IF;
END $$;

DROP INDEX IF EXISTS ani_turns_one_open;
CREATE UNIQUE INDEX IF NOT EXISTS ani_turns_one_open_user
    ON ani_turns (owner_id) WHERE status IN ('queued', 'running') AND origin = 'user';
CREATE UNIQUE INDEX IF NOT EXISTS ani_turns_one_open_hook
    ON ani_turns (owner_id) WHERE status IN ('queued', 'running') AND origin = 'hook';

CREATE TABLE IF NOT EXISTS ani_inbox (
    id            uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id      uuid NOT NULL,
    kind          text NOT NULL CHECK (kind IN ('say', 'outfit', 'act', 'event')),
    -- The validated request.
    payload       jsonb NOT NULL,
    -- The sender's id: a second item with it within 24 h is a duplicate.
    source_id     text NULL CHECK (char_length(source_id) BETWEEN 1 AND 200),
    status        text NOT NULL
                  CHECK (status IN ('queued', 'thinking', 'ready', 'delivered', 'expired', 'failed')),
    -- What she says: a say item's text, or the agent's reply to an event.
    say           text NULL,
    turn_id       uuid NULL REFERENCES ani_turns (id) ON DELETE SET NULL,
    created_at    timestamptz NOT NULL DEFAULT now(),
    ready_at      timestamptz NULL,
    delivered_at  timestamptz NULL,
    expires_at    timestamptz NOT NULL
);

CREATE INDEX IF NOT EXISTS ani_inbox_owner_status ON ani_inbox (owner_id, status, created_at);
CREATE INDEX IF NOT EXISTS ani_inbox_source ON ani_inbox (owner_id, source_id, created_at DESC)
    WHERE source_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS ani_inbox_turn ON ani_inbox (turn_id) WHERE turn_id IS NOT NULL;

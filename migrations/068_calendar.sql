-- Migration 068: Google Calendar per user (src/calendar/). The refresh token
-- is sealed with the server key (utils/Seal.hpp, calendar.google.token_key);
-- the access token lives in the process only. broken_at is set when Google
-- refuses the refresh (revoked, or seven days in a Testing project): the page
-- then asks to connect again. A state is the one-time value of one consent
-- round, bound to the user and good for ten minutes.

CREATE TABLE IF NOT EXISTS calendar_links (
    owner_id      uuid PRIMARY KEY,
    email         text NOT NULL DEFAULT '',
    token_sealed  text NOT NULL,
    token_nonce   text NOT NULL,
    broken_at     timestamptz NULL,
    created_at    timestamptz NOT NULL DEFAULT now(),
    updated_at    timestamptz NOT NULL DEFAULT now()
);

DROP TRIGGER IF EXISTS calendar_links_touch_updated_at ON calendar_links;
CREATE TRIGGER calendar_links_touch_updated_at BEFORE UPDATE ON calendar_links
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE TABLE IF NOT EXISTS calendar_states (
    state       text PRIMARY KEY,
    owner_id    uuid NOT NULL,
    created_at  timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS calendar_states_created ON calendar_states (created_at);

ALTER TABLE ani_actions DROP CONSTRAINT IF EXISTS ani_actions_kind_check;
ALTER TABLE ani_actions ADD CONSTRAINT ani_actions_kind_check
    CHECK (kind IN ('create_task', 'complete_task', 'check_habit', 'add_note', 'tg_send', 'tg_edit',
                    'calendar_add', 'calendar_move', 'calendar_remove'));

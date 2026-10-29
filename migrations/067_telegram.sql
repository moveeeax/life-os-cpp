-- Migration 067: Ani and the owner's Telegram (src/telegram/). One link at
-- most: the user the gateway account serves (the token is server config, never
-- here), with the account's own Telegram id and @username for spotting
-- mentions. A cursor per chat is the latest message the poll has looked at, so
-- every minute only the new ones are read. Ani's agent proposes a Telegram
-- message to send or edit like her other changes, so ani_actions takes the two
-- kinds.

CREATE TABLE IF NOT EXISTS telegram_link (
    owner_id    uuid PRIMARY KEY,
    -- Always true: the unique index below allows one row.
    single      boolean NOT NULL DEFAULT true CHECK (single),
    me_id       text NOT NULL,
    username    text NOT NULL DEFAULT '',
    polled_at   timestamptz NULL,
    -- Cursors set: the first poll only marks where the chats are.
    primed      boolean NOT NULL DEFAULT false,
    last_error  text NULL,
    created_at  timestamptz NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX IF NOT EXISTS telegram_link_single ON telegram_link (single);

CREATE TABLE IF NOT EXISTS telegram_cursors (
    owner_id    uuid NOT NULL,
    chat_id     text NOT NULL,
    last_id     bigint NOT NULL,
    updated_at  timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, chat_id)
);

ALTER TABLE ani_actions DROP CONSTRAINT IF EXISTS ani_actions_kind_check;
ALTER TABLE ani_actions ADD CONSTRAINT ani_actions_kind_check
    CHECK (kind IN ('create_task', 'complete_task', 'check_habit', 'add_note', 'tg_send', 'tg_edit'));

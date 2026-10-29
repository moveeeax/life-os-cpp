-- Migration 070: the Telegram follow-ups of 1.52.0. Private chats waiting
-- for the owner's answer (their newest message is incoming and older than a
-- day), refreshed hourly by the poll; chats Ani never tells about; and the
-- switch that sends the weekly review to the owner's Saved Messages, with the
-- ISO week it last went for.

CREATE TABLE IF NOT EXISTS telegram_waiting (
    owner_id    uuid NOT NULL,
    chat_id     text NOT NULL,
    title       text NOT NULL DEFAULT '',
    since       timestamptz NOT NULL,
    PRIMARY KEY (owner_id, chat_id)
);

CREATE TABLE IF NOT EXISTS telegram_quiet (
    owner_id    uuid NOT NULL,
    chat_id     text NOT NULL,
    title       text NOT NULL DEFAULT '' CHECK (char_length(title) <= 200),
    created_at  timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, chat_id)
);

ALTER TABLE telegram_link ADD COLUMN IF NOT EXISTS review_to_self boolean NOT NULL DEFAULT false;
ALTER TABLE telegram_link ADD COLUMN IF NOT EXISTS review_sent_week text NULL;
ALTER TABLE telegram_link ADD COLUMN IF NOT EXISTS waiting_at timestamptz NULL;

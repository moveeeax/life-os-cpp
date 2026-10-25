-- Migration 060: clearing the chat with Ani's agent starts a new conversation.
-- The worker talks to the agent's gateway in a session keyed by the user (the
-- `user` field of chat completions); chat_session is the number of times the
-- user cleared the chat, and a non-zero one becomes part of that key, so the
-- agent no longer sees the cleared messages. 0 keeps the key of before.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

ALTER TABLE ani_agents ADD COLUMN IF NOT EXISTS chat_session integer NOT NULL DEFAULT 0
    CHECK (chat_session >= 0);

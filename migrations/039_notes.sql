-- Migration 039: notes — private Markdown notes with the fields of a
-- tarassov.me blog post (slug, title, summary, body, topic, tags), so a note can
-- be sent there later; linked to a project or course and to the task it came from.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS notes (
    id            uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id      uuid        NOT NULL,
    slug          text        NOT NULL CHECK (slug ~ '^[a-z0-9]+(-[a-z0-9]+)*$' AND length(slug) <= 120),
    title         text        NOT NULL CHECK (length(title) BETWEEN 1 AND 200),
    summary       text        NOT NULL DEFAULT '' CHECK (length(summary) <= 500),
    body          text        NOT NULL DEFAULT '' CHECK (length(body) <= 200000),
    topic         text        NOT NULL DEFAULT '' CHECK (length(topic) <= 60),
    tags          text[]      NOT NULL DEFAULT '{}' CHECK (cardinality(tags) <= 20),
    -- private now; "published" is kept for sending a note to the blog later.
    status        text        NOT NULL DEFAULT 'private' CHECK (status IN ('private', 'published')),
    project_id    uuid        REFERENCES projects (id) ON DELETE SET NULL,
    task_id       uuid        REFERENCES task_items (id) ON DELETE SET NULL,
    published_at  timestamptz,
    created_at    timestamptz NOT NULL DEFAULT now(),
    updated_at    timestamptz NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX IF NOT EXISTS notes_owner_slug ON notes (owner_id, slug);
CREATE INDEX IF NOT EXISTS notes_owner_created ON notes (owner_id, created_at DESC);
CREATE INDEX IF NOT EXISTS notes_project ON notes (project_id) WHERE project_id IS NOT NULL;

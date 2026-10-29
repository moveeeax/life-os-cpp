-- Migration 071: what Ani keeps about the person, kept by Life OS so they can
-- read, change and delete it in Account (her own notes in the pod stay hers),
-- and the kinds of her lines they muted with "Not about this".

CREATE TABLE IF NOT EXISTS ani_facts (
    id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid NOT NULL,
    text        text NOT NULL CHECK (char_length(text) BETWEEN 1 AND 200),
    -- Where it came from: chat (she remembered it), page (the person wrote it).
    source      text NOT NULL DEFAULT 'chat' CHECK (source IN ('chat', 'page')),
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS ani_facts_owner ON ani_facts (owner_id, created_at);

DROP TRIGGER IF EXISTS ani_facts_touch_updated_at ON ani_facts;
CREATE TRIGGER ani_facts_touch_updated_at BEFORE UPDATE ON ani_facts
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

ALTER TABLE ani_prefs ADD COLUMN IF NOT EXISTS muted text[] NOT NULL DEFAULT '{}';

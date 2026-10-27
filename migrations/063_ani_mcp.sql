-- Migration 063: Ani's MCP. An API key scope "ani" that works only for her
-- MCP endpoint (/api/v1/ani/mcp), and chat turns that came through it
-- (ani_turns.origin 'mcp', at most one open per user beside the user's and
-- the webhook's own).

DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'api_keys_scope_check'
               AND pg_get_constraintdef(oid) NOT LIKE '%ani%') THEN
        ALTER TABLE api_keys DROP CONSTRAINT api_keys_scope_check;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'api_keys_scope_check') THEN
        ALTER TABLE api_keys ADD CONSTRAINT api_keys_scope_check CHECK (scope IN ('full', 'read', 'ani'));
    END IF;
END $$;

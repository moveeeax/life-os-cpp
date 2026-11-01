-- Migration 079: GitHub in Life OS (src/github/). A user links their GitHub
-- account through the site's OAuth App; the token is sealed with the server
-- key (utils/Seal.hpp, github.token_key) like the calendar's refresh token.
-- The worker job github_sync pulls the user's repositories (their own and
-- their organisations') and stores one row each with the facts the attention
-- rules read and the rules' verdict (attention). A repository gone from
-- GitHub goes at the next sync; a failed sync keeps the last list and the
-- error. A mute is "not about this": one rule silenced for one repository.
--
--   github_links: one per user. scopes is what GitHub granted, space-separated
--     ("repo read:org"). synced_at and sync_error come from the job; org_hint
--     names an organisation whose repositories came back empty or forbidden
--     (the owner approves the app in the org settings once), '' when none.
--   github_repos: keyed by GitHub's node id (a rename keeps the row); the
--     attention jsonb is the array of {rule, detail} the rules produced after
--     the mutes; task_ids maps a rule to the task made from it ("Make a task"
--     opens the existing one on a second tap).
--   github_mutes: (user, repository full name, rule).

CREATE TABLE IF NOT EXISTS github_links (
    owner_id      uuid        PRIMARY KEY,
    login         text        NOT NULL DEFAULT '',
    token_sealed  text        NOT NULL,
    token_nonce   text        NOT NULL,
    scopes        text        NOT NULL DEFAULT '',
    linked_at     timestamptz NOT NULL DEFAULT now(),
    synced_at     timestamptz NULL,
    sync_error    text        NULL,
    org_hint      text        NOT NULL DEFAULT '',
    updated_at    timestamptz NOT NULL DEFAULT now()
);

DROP TRIGGER IF EXISTS github_links_touch_updated_at ON github_links;
CREATE TRIGGER github_links_touch_updated_at BEFORE UPDATE ON github_links
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE TABLE IF NOT EXISTS github_repos (
    owner_id        uuid        NOT NULL,
    gh_node_id      text        NOT NULL,
    full_name       text        NOT NULL,
    repo_owner      text        NOT NULL,
    name            text        NOT NULL,
    visibility      text        NOT NULL DEFAULT 'public' CHECK (visibility IN ('public', 'private', 'internal')),
    fork            boolean     NOT NULL DEFAULT false,
    archived        boolean     NOT NULL DEFAULT false,
    description     text        NOT NULL DEFAULT '',
    topics          text[]      NOT NULL DEFAULT '{}',
    license         text        NOT NULL DEFAULT '',
    stars           integer     NOT NULL DEFAULT 0 CHECK (stars >= 0),
    pushed_at       timestamptz NULL,
    default_branch  text        NOT NULL DEFAULT '',
    open_issues     integer     NOT NULL DEFAULT 0 CHECK (open_issues >= 0),
    open_prs        integer     NOT NULL DEFAULT 0 CHECK (open_prs >= 0),
    open_alerts     integer     NOT NULL DEFAULT 0 CHECK (open_alerts >= 0),
    checks          text        NOT NULL DEFAULT 'none' CHECK (checks IN ('success', 'failure', 'pending', 'none')),
    has_readme      boolean     NOT NULL DEFAULT false,
    attention       jsonb       NOT NULL DEFAULT '[]'::jsonb,
    task_ids        jsonb       NOT NULL DEFAULT '{}'::jsonb,
    synced_at       timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, gh_node_id)
);

CREATE INDEX IF NOT EXISTS github_repos_owner_full_name ON github_repos (owner_id, full_name);

CREATE TABLE IF NOT EXISTS github_mutes (
    owner_id    uuid        NOT NULL,
    full_name   text        NOT NULL,
    rule        text        NOT NULL CHECK (rule IN ('checks', 'alerts', 'prs', 'issues', 'hygiene', 'stale')),
    created_at  timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, full_name, rule)
);

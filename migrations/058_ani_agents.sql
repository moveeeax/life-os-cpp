-- Migration 058: Ani's agent, an OpenClaw pod per user (src/agent/). One row
-- per user who ever woke it: the pod's state, its tokens, and which snapshot of
-- its state in S3 is current (state-<generation>.tgz; 0 = none yet). And the
-- tokens its model spent per month, counted by the worker's model proxy.
-- Deleting a row (the user is deleted) queues its snapshot objects in
-- private_file_purges (migration 051), which the files_sweep job removes.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS ani_agents (
    owner_id          uuid PRIMARY KEY,
    state             text NOT NULL DEFAULT 'stopped'
                      CHECK (state IN ('stopped', 'starting', 'running', 'stopping')),
    pod_name          text NULL,
    pod_ip            text NULL,
    -- The gateway's bearer token (the worker calls the pod with it) and the
    -- SHA-256 of the token the pod calls the model proxy with. New per run.
    gateway_token     text NULL,
    proxy_token_hash  text NULL,
    generation        integer NOT NULL DEFAULT 0 CHECK (generation >= 0),
    -- The folder of the snapshots ("<prefix>private/<owner>/ani/"), set by the
    -- first start: a deleted row leaves its snapshots to the files sweep.
    snapshot_dir      text NULL,
    started_at        timestamptz NULL,
    last_active_at    timestamptz NULL,
    last_error        text NULL,
    updated_at        timestamptz NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX IF NOT EXISTS ani_agents_proxy_token
    ON ani_agents (proxy_token_hash) WHERE proxy_token_hash IS NOT NULL;
CREATE INDEX IF NOT EXISTS ani_agents_active
    ON ani_agents (state) WHERE state <> 'stopped';

CREATE TABLE IF NOT EXISTS ani_usage (
    owner_id           uuid NOT NULL,
    month              date NOT NULL,  -- the first day of the month, UTC
    requests           integer NOT NULL DEFAULT 0,
    prompt_tokens      bigint NOT NULL DEFAULT 0,
    completion_tokens  bigint NOT NULL DEFAULT 0,
    PRIMARY KEY (owner_id, month)
);

DROP TRIGGER IF EXISTS ani_agents_touch_updated_at ON ani_agents;
CREATE TRIGGER ani_agents_touch_updated_at BEFORE UPDATE ON ani_agents
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE OR REPLACE FUNCTION ani_agents_purge() RETURNS TRIGGER AS $$
BEGIN
    IF OLD.snapshot_dir IS NOT NULL THEN
        -- The current snapshot, the one kept before it, and one a stopping run may have saved.
        INSERT INTO private_file_purges (object_key)
            SELECT OLD.snapshot_dir || 'state-' || g || '.tgz'
              FROM generate_series(GREATEST(OLD.generation - 1, 1), OLD.generation + 1) AS g
            ON CONFLICT (object_key) DO NOTHING;
    END IF;
    RETURN OLD;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS ani_agents_purge ON ani_agents;
CREATE TRIGGER ani_agents_purge AFTER DELETE ON ani_agents
    FOR EACH ROW EXECUTE FUNCTION ani_agents_purge();

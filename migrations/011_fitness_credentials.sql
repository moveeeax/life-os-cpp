-- Migration 011: fitness_credentials (xiaomi_credentials table)
-- Created: 2026-09-29T13:40:17Z
--
-- Migrations are applied in numeric order on app boot (or via
-- RUN_MIGRATIONS_ONLY=1 ./life_os_cpp). Use --verify-migrations to
-- list pending without applying.
--
-- The MigrationRunner already wraps this file in ONE transaction (under an
-- advisory lock) together with the schema_migrations bookkeeping. Do NOT add
-- BEGIN/COMMIT — an embedded COMMIT ends that transaction early and breaks
-- atomicity. Prefer idempotent DDL (IF NOT EXISTS / ON CONFLICT DO NOTHING).

-- TODO: write your DDL here. Common table skeleton (uncomment + rename):
--
-- CREATE TABLE IF NOT EXISTS your_table (
--     id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
--     name       TEXT        NOT NULL,
--     created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
--     updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
-- );
--
-- DROP TRIGGER IF EXISTS your_table_touch_updated_at ON your_table;
-- CREATE TRIGGER your_table_touch_updated_at
--     BEFORE UPDATE ON your_table
--     FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- Live passToken of the Xiaomi account. Xiaomi rotates it on every login, and
-- the previous value stops working after a while, so the cluster Secret holds
-- only the initial value (the seed) and the current one lives here.
--
-- The token is stored encrypted: crypto_secretbox_easy from libsodium, the key
-- comes from the MI_FITNESS_TOKEN_KEY environment variable and never reaches
-- the database. Ciphertext and nonce are stored in TEXT as base64, not BYTEA:
-- the project has no binary column at all, and 350 bytes are no reason to add
-- the first one.
--
-- Exactly one row: the service serves a single account (see the spec, decision 2).
CREATE TABLE IF NOT EXISTS xiaomi_credentials (
    user_id           TEXT        PRIMARY KEY,
    pass_token_sealed TEXT        NOT NULL,  -- base64(crypto_secretbox_easy(...))
    nonce             TEXT        NOT NULL,  -- base64, 24 bytes
    region            TEXT        NOT NULL DEFAULT 'cn',
    rotated_at        TIMESTAMPTZ NOT NULL DEFAULT now()
);

COMMENT ON TABLE xiaomi_credentials IS
    'Live Xiaomi passToken: rotated on every login, the Secret holds only the seed.';

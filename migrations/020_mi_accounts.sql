-- Migration 020: mi_accounts — Xiaomi credentials per Life OS user.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- Until now the service held one Xiaomi account (xiaomi_credentials, one row).
-- Each user now links their own account. The fitness data tables keep their
-- shape: their rows are keyed by the Xiaomi account id (user_id), and a user's
-- rows are the rows of the account in mi_accounts.xiaomi_user_id.

CREATE TABLE IF NOT EXISTS mi_accounts (
    owner_id          uuid        PRIMARY KEY REFERENCES users (id) ON DELETE CASCADE,
    -- One Xiaomi account links to one user.
    xiaomi_user_id    text        NOT NULL UNIQUE,
    pass_token_sealed text        NOT NULL,  -- base64(crypto_secretbox_easy(...))
    nonce             text        NOT NULL,  -- base64, 24 bytes
    region            text        NOT NULL DEFAULT 'cn',
    -- false while no region returned data for the account: the profile then
    -- offers a region selector.
    region_detected   boolean     NOT NULL DEFAULT true,
    -- reauth_required: Xiaomi refused the stored token, the user links again.
    status            text        NOT NULL DEFAULT 'ok' CHECK (status IN ('ok', 'reauth_required')),
    linked_at         timestamptz NOT NULL DEFAULT now(),
    rotated_at        timestamptz NOT NULL DEFAULT now(),
    last_ok_at        timestamptz,
    last_error        text
);

COMMENT ON TABLE mi_accounts IS
    'Xiaomi account of a user: sealed passToken (rotated by Xiaomi on login), region and link status.';

-- The single account of the single-owner era goes to the oldest administrator
-- (permission bit 0x40000000). Without a credentials row or without an
-- administrator nothing is copied.
DO $$
BEGIN
    IF to_regclass('xiaomi_credentials') IS NOT NULL THEN
        INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce, region, rotated_at, last_ok_at)
        SELECT u.id, c.user_id, c.pass_token_sealed, c.nonce, c.region, c.rotated_at, c.rotated_at
        FROM (SELECT * FROM xiaomi_credentials ORDER BY rotated_at DESC LIMIT 1) c
        CROSS JOIN LATERAL (
            SELECT us.id FROM users us JOIN roles r ON r.id = us.role_id
            WHERE (r.permissions & 1073741824) <> 0
            ORDER BY us.created_at LIMIT 1) u
        ON CONFLICT DO NOTHING;
        DROP TABLE xiaomi_credentials;
    END IF;
END $$;

-- Sync bookkeeping learns which account a row belongs to. The columns are
-- filled for the existing rows here; the per-account keys follow with the
-- per-account sync.
ALTER TABLE sync_runs  ADD COLUMN IF NOT EXISTS xiaomi_user_id text;
ALTER TABLE sync_state ADD COLUMN IF NOT EXISTS xiaomi_user_id text;
UPDATE sync_runs SET xiaomi_user_id = (SELECT xiaomi_user_id FROM mi_accounts ORDER BY linked_at LIMIT 1)
WHERE xiaomi_user_id IS NULL;
UPDATE sync_state SET xiaomi_user_id = (SELECT xiaomi_user_id FROM mi_accounts ORDER BY linked_at LIMIT 1)
WHERE xiaomi_user_id IS NULL;

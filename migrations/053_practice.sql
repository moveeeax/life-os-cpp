-- Migration 053: practice — the owner's freelance practice: clients, deals
-- with stages, and the incomes and expenses of the practice; a task may name
-- the deal it is a next step of.
--
-- Money follows the rule of the money module: an amount is positive and in
-- its own currency, nothing converted is stored, and totals are per currency.
-- Amounts are numeric(24,8), as money's (migration 028).
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS practice_clients (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    name        text        NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
    contact     text        NOT NULL DEFAULT '' CHECK (length(contact) <= 300),
    note        text        NOT NULL DEFAULT '' CHECK (length(note) <= 2000),
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS practice_clients_owner_name ON practice_clients (owner_id, name);

CREATE TABLE IF NOT EXISTS practice_deals (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    -- A client with deals stays until its deals are gone.
    client_id   uuid        NOT NULL REFERENCES practice_clients (id) ON DELETE RESTRICT,
    title       text        NOT NULL CHECK (length(title) BETWEEN 1 AND 200),
    stage       text        NOT NULL DEFAULT 'lead'
                            CHECK (stage IN ('lead', 'negotiation', 'active', 'done', 'lost')),
    value       numeric(24,8) CHECK (value > 0),
    currency    text        CHECK (currency ~ '^[A-Z]{3}$'),
    start_date  date,
    end_date    date,
    note        text        NOT NULL DEFAULT '' CHECK (length(note) <= 2000),
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now(),
    -- A value is an amount and a currency, together or not at all.
    CHECK ((value IS NULL) = (currency IS NULL)),
    CHECK (end_date IS NULL OR start_date IS NULL OR end_date >= start_date)
);
CREATE INDEX IF NOT EXISTS practice_deals_owner_stage ON practice_deals (owner_id, stage);
CREATE INDEX IF NOT EXISTS practice_deals_client ON practice_deals (client_id);

CREATE TABLE IF NOT EXISTS practice_entries (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    kind        text        NOT NULL CHECK (kind IN ('income', 'expense')),
    date        date        NOT NULL,
    amount      numeric(24,8) NOT NULL CHECK (amount > 0),
    currency    text        NOT NULL CHECK (currency ~ '^[A-Z]{3}$'),
    client_id   uuid        REFERENCES practice_clients (id) ON DELETE SET NULL,
    deal_id     uuid        REFERENCES practice_deals (id) ON DELETE SET NULL,
    note        text        NOT NULL DEFAULT '' CHECK (length(note) <= 500),
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS practice_entries_owner_date ON practice_entries (owner_id, date DESC);
CREATE INDEX IF NOT EXISTS practice_entries_client ON practice_entries (client_id) WHERE client_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS practice_entries_deal ON practice_entries (deal_id) WHERE deal_id IS NOT NULL;

-- The deal a task is a next step of (additive; deleting the deal unlinks it).
ALTER TABLE task_items ADD COLUMN IF NOT EXISTS deal_id uuid REFERENCES practice_deals (id) ON DELETE SET NULL;
CREATE INDEX IF NOT EXISTS task_items_deal ON task_items (deal_id) WHERE deal_id IS NOT NULL;

DROP TRIGGER IF EXISTS practice_clients_touch_updated_at ON practice_clients;
CREATE TRIGGER practice_clients_touch_updated_at
    BEFORE UPDATE ON practice_clients
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS practice_deals_touch_updated_at ON practice_deals;
CREATE TRIGGER practice_deals_touch_updated_at
    BEFORE UPDATE ON practice_deals
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS practice_entries_touch_updated_at ON practice_entries;
CREATE TRIGGER practice_entries_touch_updated_at
    BEFORE UPDATE ON practice_entries
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

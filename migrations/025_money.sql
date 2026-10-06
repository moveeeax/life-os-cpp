-- Migration 025: money — currencies, accounts, categories with budgets, the
-- ledger, transfers, merchant memory, daily rates and the settings of the
-- money module.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- Every row belongs to a user (owner_id). The seven invariants of the owner's
-- model (docs/superpowers/specs/2026-10-06-money-section-design.md §2) are
-- CHECKs here, not conventions of the pages:
--   1. amount is positive and in the account's currency; the sign is the type;
--   2. one account per transaction;
--   3. an exchange is one transfer row with the amount sent and received;
--   4. a bank's recalculation is an fx_adjustment pointing at its original;
--   5. no reporting currency: nothing converted is stored;
--   6. the receipt's amount and currency are their own columns;
--   7. a budget is one amount in one currency.
-- Money columns are numeric(18,4); the display rounds to the currency's decimals.

CREATE TABLE IF NOT EXISTS money_currencies (
    owner_id    uuid        NOT NULL,
    code        text        NOT NULL CHECK (code ~ '^[A-Z]{3}$'),
    name        text        NOT NULL DEFAULT '' CHECK (length(name) <= 60),
    role        text        CHECK (role IN ('primary', 'local')),
    decimals    smallint    NOT NULL DEFAULT 2 CHECK (decimals BETWEEN 0 AND 4),
    archived    boolean     NOT NULL DEFAULT false,
    created_at  timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, code)
);

CREATE TABLE IF NOT EXISTS money_accounts (
    id               uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id         uuid        NOT NULL,
    name             text        NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
    bank             text        NOT NULL DEFAULT '' CHECK (length(bank) <= 60),
    kind             text        NOT NULL DEFAULT 'card' CHECK (kind IN ('card', 'cash', 'deposit', 'other')),
    currency         text        NOT NULL,
    last4            text        NOT NULL DEFAULT '' CHECK (last4 ~ '^[0-9]{0,4}$'),
    opening_balance  numeric(18,4) NOT NULL DEFAULT 0,
    opening_date     date,
    archived         boolean     NOT NULL DEFAULT false,
    position         integer     NOT NULL DEFAULT 0,
    created_at       timestamptz NOT NULL DEFAULT now(),
    updated_at       timestamptz NOT NULL DEFAULT now(),
    FOREIGN KEY (owner_id, currency) REFERENCES money_currencies (owner_id, code)
);
CREATE INDEX IF NOT EXISTS idx_money_accounts_owner ON money_accounts (owner_id, position, name);

CREATE TABLE IF NOT EXISTS money_categories (
    id               uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id         uuid        NOT NULL,
    name             text        NOT NULL CHECK (length(name) BETWEEN 1 AND 60),
    kind             text        NOT NULL DEFAULT 'expense' CHECK (kind IN ('expense', 'income')),
    flexibility      text        NOT NULL DEFAULT 'variable' CHECK (flexibility IN ('fixed', 'variable')),
    budget_max       numeric(18,4) CHECK (budget_max > 0),
    budget_currency  text,
    archived         boolean     NOT NULL DEFAULT false,
    position         integer     NOT NULL DEFAULT 0,
    created_at       timestamptz NOT NULL DEFAULT now(),
    updated_at       timestamptz NOT NULL DEFAULT now(),
    -- Invariant 7: a budget is an amount and a currency, together or not at all.
    CHECK ((budget_max IS NULL) = (budget_currency IS NULL)),
    FOREIGN KEY (owner_id, budget_currency) REFERENCES money_currencies (owner_id, code)
);
CREATE INDEX IF NOT EXISTS idx_money_categories_owner ON money_categories (owner_id, position, name);

CREATE TABLE IF NOT EXISTS money_transactions (
    id                uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id          uuid        NOT NULL,
    type              text        NOT NULL CHECK (type IN ('income', 'expense', 'fx_adjustment')),
    date              date        NOT NULL,
    time              time,
    -- Invariant 2: one account. The type says whether the money left or entered it.
    account_id        uuid        NOT NULL REFERENCES money_accounts (id) ON DELETE RESTRICT,
    -- Invariant 1: positive, in the account's currency (the account's currency is read on the way out).
    amount            numeric(18,4) NOT NULL CHECK (amount > 0),
    category_id       uuid        REFERENCES money_categories (id) ON DELETE RESTRICT,
    merchant          text        NOT NULL DEFAULT '' CHECK (length(merchant) <= 200),
    merchant_key      text        NOT NULL DEFAULT '' CHECK (length(merchant_key) <= 200),
    name              text        NOT NULL CHECK (length(name) BETWEEN 1 AND 200),
    -- Invariant 6: the receipt apart from the amount that left the account.
    receipt_amount    numeric(18,4) CHECK (receipt_amount > 0),
    receipt_currency  text        CHECK (receipt_currency ~ '^[A-Z]{3}$'),
    fx_note           text        NOT NULL DEFAULT '' CHECK (length(fx_note) <= 200),
    -- Invariant 4: an adjustment points at its original and only an adjustment does.
    adjusts_id        uuid        REFERENCES money_transactions (id) ON DELETE CASCADE,
    trip              text        NOT NULL DEFAULT '' CHECK (length(trip) <= 60),
    note              text        NOT NULL DEFAULT '' CHECK (length(note) <= 2000),
    source            text        NOT NULL DEFAULT 'manual' CHECK (source IN ('manual', 'text', 'receipt', 'api', 'import')),
    status            text        NOT NULL DEFAULT 'posted' CHECK (status IN ('posted', 'pending')),
    external_id       text        CHECK (length(external_id) <= 120),
    created_at        timestamptz NOT NULL DEFAULT now(),
    updated_at        timestamptz NOT NULL DEFAULT now(),
    CHECK ((receipt_amount IS NULL) = (receipt_currency IS NULL)),
    CHECK ((type = 'fx_adjustment') = (adjusts_id IS NOT NULL)),
    CHECK (type = 'fx_adjustment' OR category_id IS NOT NULL)
);
CREATE INDEX IF NOT EXISTS idx_money_transactions_owner_date ON money_transactions (owner_id, date DESC, id);
CREATE INDEX IF NOT EXISTS idx_money_transactions_account ON money_transactions (owner_id, account_id, date);
CREATE INDEX IF NOT EXISTS idx_money_transactions_pending ON money_transactions (owner_id) WHERE status = 'pending';
CREATE INDEX IF NOT EXISTS idx_money_transactions_merchant ON money_transactions (owner_id, merchant_key);
CREATE INDEX IF NOT EXISTS idx_money_transactions_adjusts ON money_transactions (adjusts_id) WHERE adjusts_id IS NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS money_transactions_owner_external
    ON money_transactions (owner_id, external_id) WHERE external_id IS NOT NULL;

CREATE TABLE IF NOT EXISTS money_transfers (
    id                uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id          uuid        NOT NULL,
    date              date        NOT NULL,
    from_account_id   uuid        NOT NULL REFERENCES money_accounts (id) ON DELETE RESTRICT,
    to_account_id     uuid        NOT NULL REFERENCES money_accounts (id) ON DELETE RESTRICT,
    -- Invariant 3: sent in the source currency, received in the target's; null when they are the same.
    amount_sent       numeric(18,4) NOT NULL CHECK (amount_sent > 0),
    amount_received   numeric(18,4) CHECK (amount_received > 0),
    fee               numeric(18,4) CHECK (fee >= 0),
    name              text        NOT NULL DEFAULT '' CHECK (length(name) <= 200),
    note              text        NOT NULL DEFAULT '' CHECK (length(note) <= 2000),
    external_id       text        CHECK (length(external_id) <= 120),
    created_at        timestamptz NOT NULL DEFAULT now(),
    updated_at        timestamptz NOT NULL DEFAULT now(),
    CHECK (from_account_id <> to_account_id)
);
CREATE INDEX IF NOT EXISTS idx_money_transfers_owner_date ON money_transfers (owner_id, date DESC, id);
CREATE INDEX IF NOT EXISTS idx_money_transfers_from ON money_transfers (owner_id, from_account_id, date);
CREATE INDEX IF NOT EXISTS idx_money_transfers_to ON money_transfers (owner_id, to_account_id, date);
CREATE UNIQUE INDEX IF NOT EXISTS money_transfers_owner_external
    ON money_transfers (owner_id, external_id) WHERE external_id IS NOT NULL;

-- What a merchant usually is: the category the person picked last, how often.
CREATE TABLE IF NOT EXISTS money_merchants (
    owner_id      uuid        NOT NULL,
    merchant_key  text        NOT NULL CHECK (length(merchant_key) BETWEEN 1 AND 200),
    display_name  text        NOT NULL CHECK (length(display_name) BETWEEN 1 AND 200),
    category_id   uuid        REFERENCES money_categories (id) ON DELETE SET NULL,
    account_id    uuid        REFERENCES money_accounts (id) ON DELETE SET NULL,
    times         integer     NOT NULL DEFAULT 1 CHECK (times >= 0),
    last_seen     date        NOT NULL,
    PRIMARY KEY (owner_id, merchant_key)
);

-- Daily quotes against the US dollar, shared by every user. Not a bank's rate:
-- a bank's recalculation is an fx_adjustment row.
CREATE TABLE IF NOT EXISTS money_fx_rates (
    date        date        NOT NULL,
    quote       text        NOT NULL CHECK (quote ~ '^[A-Z]{3}$'),
    per_usd     numeric(20,8) NOT NULL CHECK (per_usd > 0),
    fetched_at  timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (date, quote)
);

CREATE TABLE IF NOT EXISTS money_settings (
    owner_id            uuid        PRIMARY KEY,
    view_currency       text        CHECK (view_currency ~ '^[A-Z]{3}$'),
    advisor_enabled     boolean     NOT NULL DEFAULT false,
    advisor_weekday     smallint    NOT NULL DEFAULT 1 CHECK (advisor_weekday BETWEEN 1 AND 7),
    advisor_currencies  text[]      NOT NULL DEFAULT '{}',
    advisor_note        text        NOT NULL DEFAULT '' CHECK (length(advisor_note) <= 2000),
    updated_at          timestamptz NOT NULL DEFAULT now()
);

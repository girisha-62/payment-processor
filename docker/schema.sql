-- Schema for the "extend with real persistence" step described in the README.
-- Not used by the current in-memory prototype, but matches its domain model
-- so swapping AccountService for a DB-backed version is a direct mapping.

CREATE TABLE customers (
    id          SERIAL PRIMARY KEY,
    full_name   TEXT NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE accounts (
    id              SERIAL PRIMARY KEY,
    customer_id     INTEGER NOT NULL REFERENCES customers(id),
    balance_cents   BIGINT NOT NULL DEFAULT 0 CHECK (balance_cents >= 0),
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TYPE transaction_type AS ENUM
    ('AUTHORIZATION', 'DEBIT', 'CREDIT', 'REFUND', 'REVERSAL', 'TRANSFER');

CREATE TYPE transaction_status AS ENUM
    ('PENDING', 'SUCCESS', 'FAILED', 'DUPLICATE');

CREATE TABLE transactions (
    id                  BIGSERIAL PRIMARY KEY,
    txn_id              TEXT NOT NULL UNIQUE,
    idempotency_key     TEXT NOT NULL UNIQUE,
    type                transaction_type NOT NULL,
    from_account_id     INTEGER NOT NULL REFERENCES accounts(id),
    to_account_id       INTEGER REFERENCES accounts(id),
    amount_cents        BIGINT NOT NULL CHECK (amount_cents > 0),
    status              transaction_status NOT NULL DEFAULT 'PENDING',
    message             TEXT,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now(),
    processed_at        TIMESTAMPTZ
);

-- Fast duplicate-detection and history lookups.
CREATE INDEX idx_transactions_from_account ON transactions(from_account_id);
CREATE INDEX idx_transactions_status ON transactions(status);

CREATE TABLE IF NOT EXISTS transactions (
    id VARCHAR(37) PRIMARY KEY,
    transaction_date TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS entries (
    id VARCHAR(37) PRIMARY KEY,
    transaction_id VARCHAR(37) NOT NULL,
    account_id VARCHAR(37) NOT NULL,
    amount INTEGER NOT NULL,
    created_at TEXT NOT NULL,
    FOREIGN KEY (transaction_id) REFERENCES transactions (id),
    FOREIGN KEY (account_id) REFERENCES accounts (id)
);

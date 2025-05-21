CREATE TABLE IF NOT EXISTS portfolios (
    id VARCHAR(37) PRIMARY KEY,
    name VARCHAR(20) NOT NULL UNIQUE,
    currency VARCHAR(3) NOT NULL,
    created_at VARCHAR(24) NOT NULL,
    updated_at VARCHAR(24) NOT NULL
);

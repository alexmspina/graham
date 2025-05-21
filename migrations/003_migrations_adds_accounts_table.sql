CREATE TABLE IF NOT EXISTS accounts (
    id VARCHAR(37) PRIMARY KEY,
    asset_id VARCHAR(37) NOT NULL,
    portfolio_id VARCHAR(37) NOT NULL,
    created_at VARCHAR(24) NOT NULL,
    updated_at VARCHAR(24) NOT NULL,
    FOREIGN KEY (asset_id) REFERENCES assets(id),
    FOREIGN KEY (portfolio_id) REFERENCES portfolios(id)
    UNIQUE (asset_id, portfolio_id)
);
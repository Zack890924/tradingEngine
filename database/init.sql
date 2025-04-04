-- Drop tables if they exist
DROP TABLE IF EXISTS executions;
DROP TABLE IF EXISTS orders;
DROP TABLE IF EXISTS positions;
DROP TABLE IF EXISTS symbols;
DROP TABLE IF EXISTS accounts;

-- Create tables
CREATE TABLE accounts (
    account_id VARCHAR(255) PRIMARY KEY,
    balance NUMERIC(19, 4) NOT NULL CHECK (balance >= 0)
);

CREATE TABLE symbols (
    symbol VARCHAR(255) PRIMARY KEY
);

CREATE TABLE positions (
    account_id VARCHAR(255),
    symbol VARCHAR(255),
    amount NUMERIC(19, 4) NOT NULL DEFAULT 0 CHECK (amount >= 0),
    PRIMARY KEY (account_id, symbol),
    FOREIGN KEY (account_id) REFERENCES accounts(account_id),
    FOREIGN KEY (symbol) REFERENCES symbols(symbol)
);

CREATE TABLE orders (
    order_id SERIAL PRIMARY KEY,
    account_id VARCHAR(255) NOT NULL,
    symbol VARCHAR(255) NOT NULL,
    amount NUMERIC(19, 4) NOT NULL,
    limit_price NUMERIC(19, 4) NOT NULL,
    open_amount NUMERIC(19, 4) NOT NULL,
    status VARCHAR(10) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (account_id) REFERENCES accounts(account_id),
    FOREIGN KEY (symbol) REFERENCES symbols(symbol)
);

--executions
CREATE TABLE executions (
    execution_id SERIAL PRIMARY KEY,
    buy_order_id INTEGER NOT NULL,
    sell_order_id INTEGER NOT NULL,
    symbol VARCHAR(255) NOT NULL,
    amount NUMERIC(19, 4) NOT NULL,
    price NUMERIC(19, 4) NOT NULL,
    executed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (buy_order_id) REFERENCES orders(order_id),
    FOREIGN KEY (sell_order_id) REFERENCES orders(order_id)
);


ALTER TABLE symbols 
    ADD CONSTRAINT symbols_pkey PRIMARY KEY(symbol);


-- Create indexes for better performance
CREATE INDEX idx_orders_symbol_status_amount ON orders(symbol, status, amount);
CREATE INDEX idx_orders_created_at ON orders(created_at);
CREATE INDEX idx_executions_buy_sell_order ON executions(buy_order_id, sell_order_id);
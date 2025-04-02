#include "accountRepository.hpp"
#include "dbConnection.hpp"
#include <pqxx/pqxx>

bool AccountRepository::createAccount(const std::string& accountId, double balance) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        
        // Check if account already exists
        pqxx::result r = txn.exec_params(
            "SELECT 1 FROM accounts WHERE account_id = $1",
            accountId
        );
        
        if (!r.empty()) {
            return false; // Account already exists
        }
        
        // Create the account
        txn.exec_params(
            "INSERT INTO accounts (account_id, balance) VALUES ($1, $2)",
            accountId,
            balance
        );
        
        txn.commit();
        return true;
    }
    catch (const std::exception& e) {
        // Log the error
        return false;
    }
}

bool AccountRepository::accountExists(const std::string& accountId) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        pqxx::result r = txn.exec_params(
            "SELECT 1 FROM accounts WHERE account_id = $1",
            accountId
        );
        return !r.empty();
    }
    catch (const std::exception& e) {
        // Log the error
        return false;
    }
}

double AccountRepository::getBalance(const std::string& accountId) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        pqxx::result r = txn.exec_params(
            "SELECT balance FROM accounts WHERE account_id = $1",
            accountId
        );
        
        if (r.empty()) {
            return -1; // Account not found
        }
        
        return r[0][0].as<double>();
    }
    catch (const std::exception& e) {
        // Log the error
        return -1;
    }
}

bool AccountRepository::updateBalance(const std::string& accountId, double newBalance) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        pqxx::result r = txn.exec_params(
            "UPDATE accounts SET balance = $1 WHERE account_id = $2",
            newBalance,
            accountId
        );
        
        txn.commit();
        return true;
    }
    catch (const std::exception& e) {
        // Log the error
        return false;
    }
}

bool AccountRepository::addPosition(const std::string& accountId, const std::string& symbol, double amount) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        
        // Ensure symbol exists
        pqxx::result r = txn.exec_params(
            "SELECT 1 FROM symbols WHERE symbol = $1",
            symbol
        );
        
        if (r.empty()) {
            // Create symbol if it doesn't exist
            txn.exec_params(
                "INSERT INTO symbols (symbol) VALUES ($1)",
                symbol
            );
        }
        
        // Check if position already exists
        r = txn.exec_params(
            "SELECT 1 FROM positions WHERE account_id = $1 AND symbol = $2",
            accountId,
            symbol
        );
        
        if (r.empty()) {
            // Create new position
            txn.exec_params(
                "INSERT INTO positions (account_id, symbol, amount) VALUES ($1, $2, $3)",
                accountId,
                symbol,
                amount
            );
        } else {
            // Update existing position
            txn.exec_params(
                "UPDATE positions SET amount = amount + $1 WHERE account_id = $2 AND symbol = $3",
                amount,
                accountId,
                symbol
            );
        }
        
        txn.commit();
        return true;
    }
    catch (const std::exception& e) {
        // Log the error
        return false;
    }
}

bool AccountRepository::updatePosition(const std::string& accountId, const std::string& symbol, double amount) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        
        pqxx::result r = txn.exec_params(
            "UPDATE positions SET amount = $1 WHERE account_id = $2 AND symbol = $3",
            amount,
            accountId,
            symbol
        );
        
        txn.commit();
        return true;
    }
    catch (const std::exception& e) {
        // Log the error
        return false;
    }
}

double AccountRepository::getPosition(const std::string& accountId, const std::string& symbol) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        pqxx::result r = txn.exec_params(
            "SELECT amount FROM positions WHERE account_id = $1 AND symbol = $2",
            accountId,
            symbol
        );
        
        if (r.empty()) {
            return 0; // No position for this symbol
        }
        
        return r[0][0].as<double>();
    }
    catch (const std::exception& e) {
        // Log the error
        return -1;
    }
}
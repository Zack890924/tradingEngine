#include "accountRepository.hpp"
#include "dbConnection.hpp"
#include <pqxx/pqxx>
#include <map>
#include <iostream>

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


bool AccountRepository::executeSQL(const std::string& sql) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        txn.exec(sql);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        // log
        return false;
    }
}

std::map<std::string, double> AccountRepository::getAllPositions(const std::string& accountId) {
    std::map<std::string, double> result;
    try {
        pqxx::work txn(DBConnection::getConnection());
        pqxx::result r = txn.exec_params(
            "SELECT symbol, amount FROM positions WHERE account_id = $1",
            accountId
        );

        for (const auto& row : r) {
            std::string symbol = row[0].as<std::string>();
            double amount = row[1].as<double>();
            result[symbol] = amount;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[getAllPositions] Exception: " << e.what() << std::endl;
    }
    return result;
}

// Transaction-aware versions
bool AccountRepository::accountExists(pqxx::work& txn, const std::string& accountId) {
    try {
        pqxx::result r = txn.exec_params(
            "SELECT 1 FROM accounts WHERE account_id = $1",
            accountId
        );
        return !r.empty();
    }
    catch (const std::exception& e) {
        return false;
    }
}

double AccountRepository::getBalance(pqxx::work& txn, const std::string& accountId) {
    try {
        pqxx::result r = txn.exec_params(
            "SELECT balance FROM accounts WHERE account_id = $1 FOR UPDATE",
            accountId
        );

        if (r.empty()) {
            return -1;
        }

        return r[0][0].as<double>();
    }
    catch (const std::exception& e) {
        return -1;
    }
}

bool AccountRepository::updateBalance(pqxx::work& txn, const std::string& accountId, double newBalance) {
    try {
        pqxx::result r = txn.exec_params(
            "UPDATE accounts SET balance = $1 WHERE account_id = $2",
            newBalance,
            accountId
        );
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

double AccountRepository::getPosition(pqxx::work& txn, const std::string& accountId, const std::string& symbol) {
    try {
        pqxx::result r = txn.exec_params(
            "SELECT amount FROM positions WHERE account_id = $1 AND symbol = $2 FOR UPDATE",
            accountId,
            symbol
        );

        if (r.empty()) {
            return 0;
        }

        return r[0][0].as<double>();
    }
    catch (const std::exception& e) {
        return -1;
    }
}

bool AccountRepository::updatePosition(pqxx::work& txn, const std::string& accountId, const std::string& symbol, double amount) {
    try {
        pqxx::result r = txn.exec_params(
            "UPDATE positions SET amount = $1 WHERE account_id = $2 AND symbol = $3",
            amount,
            accountId,
            symbol
        );
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

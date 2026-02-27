#ifndef ACCOUNT_REPOSITORY_HPP
#define ACCOUNT_REPOSITORY_HPP

#include "account.hpp"
#include <memory>
#include <string>
#include <vector>
#include <map>

// Forward declare pqxx::work
namespace pqxx {
    class work;
}

class AccountRepository {
public:
    bool createAccount(const std::string& accountId, double balance);
    bool accountExists(const std::string& accountId);
    double getBalance(const std::string& accountId);
    bool updateBalance(const std::string& accountId, double newBalance);
    bool addPosition(const std::string& accountId, const std::string& symbol, double amount);
    bool updatePosition(const std::string& accountId, const std::string& symbol, double amount);
    double getPosition(const std::string& accountId, const std::string& symbol);
    bool executeSQL(const std::string& sql);
    std::map<std::string, double> getAllPositions(const std::string& accountId);

    // Transaction-aware versions (accept existing transaction)
    bool accountExists(pqxx::work& txn, const std::string& accountId);
    double getBalance(pqxx::work& txn, const std::string& accountId);
    bool updateBalance(pqxx::work& txn, const std::string& accountId, double newBalance);
    double getPosition(pqxx::work& txn, const std::string& accountId, const std::string& symbol);
    bool updatePosition(pqxx::work& txn, const std::string& accountId, const std::string& symbol, double amount);
};

#endif
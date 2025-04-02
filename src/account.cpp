//src/account.cpp


#include "account.hpp"
#include <stdexcept>

Account::Account(const std::string id, double balance) : account_id(id), balance(balance), frozen_balance(0.0) {}
Account::Account() : account_id(""), balance(0.0), frozen_balance(0.0) {}

const std::string& Account::getAccountId() const {
    return account_id;
}

double Account::getTotalBalance() const {
    return balance;
} 

double Account::getFrozenBalance() const {
    return frozen_balance;
}

double Account::getAvailableBalance() const {
    return balance - frozen_balance;
}

void Account::addBalance(double amount){
    balance += amount;
}

bool Account::freezeBalance(double amount){
    if(amount <= getAvailableBalance()){
        frozen_balance += amount;
        return true;
    }
    return false;
}

bool Account::unfreezeBalance(double amount){
    if(amount <= frozen_balance){
        frozen_balance -= amount;
        return true;
    }
    return false;
}

void Account::deductFrozenFunds(double amount){
    if(amount > frozen_balance){
        throw std::runtime_error("Attempting to deduct more funds than frozen");
    }
    
    frozen_balance -= amount;
    balance -= amount;
    
    if(balance < 0){
        throw std::runtime_error("Deduction resulted in negative balance");
    }
}

double Account::getTotalPosition(const std::string &symbol) const {
    auto it = positions.find(symbol);
    if(it != positions.end()){
        return it->second;
    }
    return 0;
}

double Account::getFrozenPosition(const std::string &symbol) const {
    auto it = frozen_positions.find(symbol);
    if(it != frozen_positions.end()){
        return it->second;
    }
    return 0;
}

double Account::getAvailablePosition(const std::string &symbol) const {
    return getTotalPosition(symbol) - getFrozenPosition(symbol);
}

void Account::addPosition(const std::string &symbol, double shares){
    if(shares < 0){
        throw std::invalid_argument("Cannot add negative shares");
    }
    positions[symbol] += shares;
}

bool Account::freezePosition(const std::string &symbol, double shares){
    if(shares <= getAvailablePosition(symbol)){
        frozen_positions[symbol] += shares;
        return true;
    }
    return false;
}

void Account::unfreezePosition(const std::string &symbol, double shares){
    auto it = frozen_positions.find(symbol);
    if(it == frozen_positions.end() || it->second < shares){
        throw std::runtime_error("Attempting to unfreeze more shares than frozen");
    }
    
    frozen_positions[symbol] -= shares;
    if(frozen_positions[symbol] <= 0){
        frozen_positions.erase(symbol);
    }
}

void Account::deductFrozenPosition(const std::string &symbol, double shares){
    auto frozenIt = frozen_positions.find(symbol);
    if(frozenIt == frozen_positions.end() || frozenIt->second < shares){
        throw std::runtime_error("Attempting to deduct more shares than frozen");
    }
    
    auto posIt = positions.find(symbol);
    if(posIt == positions.end() || posIt->second < shares){
        throw std::runtime_error("Attempting to deduct more shares than owned");
    }
    
    frozen_positions[symbol] -= shares;
    if(frozen_positions[symbol] <= 0){
        frozen_positions.erase(symbol);
    }
    
    positions[symbol] -= shares;
    if(positions[symbol] <= 0){
        positions.erase(symbol);
    }
}


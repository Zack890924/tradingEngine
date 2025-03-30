//src/account.cpp


#include "account.hpp"
#include <stdexcept>

Account::Account(const std::string id, double balance) : account_id(id), balance(balance), frozen_balance(0.0) {}
Account::Account() : account_id(""), balance(0.0), frozen_balance(0.0) {}

const std::string& Account::getAccountId() const {
    // std::lock_guard<std::mutex> lock(mtx);
    return account_id;
}

double Account::getTotalBalance() const {
    // std::lock_guard<std::mutex> lock(mtx);
    return balance;
} 

double Account::getFrozenBalance() const {
    // std::lock_guard<std::mutex> lock(mtx);
    return frozen_balance;
}

double Account::getAvailableBalance() const {
    // std::lock_guard<std::mutex> lock(mtx);
    return balance - frozen_balance;
}

void Account::addBalance(double amount){
    // std::lock_guard<std::mutex> lock(mtx);
    balance += amount;
}


bool Account::freezeBalance(double amount){
    // std::lock_guard<std::mutex> lock(mtx);
    if(amount <= getAvailableBalance()){
        frozen_balance += amount;
        return true;
    }
    return false;
}

bool Account::unfreezeBalance(double amount){
    // std::lock_guard<std::mutex> lock(mtx);
    double actual = std::min(amount, frozen_balance);
    frozen_balance -= actual;
    return (actual > 0); 
}

void Account::deductFrozenFunds(double amount){
    // std::lock_guard<std::mutex> lock(mtx);
    if(amount > frozen_balance){
        throw std::runtime_error("amount exceeds frozen balance");
    }
    frozen_balance -= amount;
    balance -= amount;
    if(balance < 0){
        balance = 0;
    }
}

double Account::getTotalPosition(const std::string &symbol) const {
    // std::lock_guard<std::mutex> lock(mtx);
    auto it = positions.find(symbol);
    if(it != positions.end()){
        return it->second;
    }
    return 0.0;
}

double Account::getFrozenPosition(const std::string &symbol) const {
    // std::lock_guard<std::mutex> lock(mtx);
    auto it = frozen_positions.find(symbol);
    if(it != frozen_positions.end()){
        return it->second;
    }
    return 0.0;
}

double Account::getAvailablePosition(const std::string &symbol) const {
    // std::lock_guard<std::mutex> lock(mtx);
    return getTotalPosition(symbol) - getFrozenPosition(symbol);
}

void Account::addPosition(const std::string &symbol, double shares){
    if(shares < 0){
        throw std::runtime_error("Shares cannot be negative");
    }
    // std::lock_guard<std::mutex> lock(mtx);
    positions[symbol] += shares;
}

bool Account::freezePosition(const std::string &symbol, double shares){
    if(shares < 0){
        throw std::runtime_error("Shares cannot be negative");
    }
    // std::lock_guard<std::mutex> lock(mtx);
    double available = getAvailablePosition(symbol);
    if (shares > available){
        return false;
    }
    frozen_positions[symbol] += shares;
    return true;
}

//order cancelled, unfreeze the position
void Account::unfreezePosition(const std::string &symbol, double shares){
    if(shares < 0){
        throw std::runtime_error("Shares cannot be negative");
    }
    // std::lock_guard<std::mutex> lock(mtx);
    auto it = frozen_positions.find(symbol);
    if(it == frozen_positions.end()){
        return;
    }
    double actual = std::min(it->second, shares);
    it->second -= actual;
    if(it->second <= 0) {
        frozen_positions.erase(it);
    }
}

//order executed, deduct frozen position
void Account::deductFrozenPosition(const std::string &symbol, double shares){
    if(shares < 0){
        throw std::runtime_error("Shares cannot be negative");
    }
    // std::lock_guard<std::mutex> lock(mtx);

    auto itF = frozen_positions.find(symbol);
    if (itF == frozen_positions.end()) {
        throw std::runtime_error("Symbol not found in frozen positions: " + symbol);
    }
    if (shares > itF->second) {
        throw std::runtime_error("Deducting more shares than frozen for: " + symbol);
    }

    itF->second -= shares;
    if (itF->second <= 0) {
        frozen_positions.erase(itF);
    }

    auto itPos = positions.find(symbol);
    if (itPos == positions.end()) {
        throw std::runtime_error("Symbol not found in total positions: " + symbol);
    }

    itPos->second -= shares;
    if (itPos->second <= 0) {
        positions.erase(itPos); 
    }
}


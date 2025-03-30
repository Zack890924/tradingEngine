#ifndef ACCOUNT_HPP
#define ACCOUNT_HPP
#include <unordered_map>
#include <string>
#include <mutex>

class Account{
    private:
        std::string account_id;
        double balance;
        double frozen_balance;
        std::unordered_map<std::string, double> positions;
        std::unordered_map<std::string, double> frozen_positions;

        // mutable std::mutex mtx;
    public:
        Account(const std::string id, double balance);
        Account();

        const std::string& getAccountId() const;

        double getTotalBalance() const;
        double getFrozenBalance() const;
        double getAvailableBalance() const;


        bool freezeBalance(double amount);
        bool unfreezeBalance(double amount);
        void deductFrozenFunds(double amount);
        void addBalance(double amount);
        



        
        double getTotalPosition(const std::string &symbol) const;
        double getFrozenPosition(const std::string &symbol) const;
        double getAvailablePosition(const std::string &symbol) const;


        bool freezePosition(const std::string &symbol, double shares);
        void unfreezePosition(const std::string &symbol, double shares);
        void addPosition(const std::string &symbol, double shares);
        void deductFrozenPosition(const std::string &symbol, double shares);
};


#endif //ACCOUNT_HPP
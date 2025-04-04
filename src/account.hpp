#ifndef ACCOUNT_HPP
#define ACCOUNT_HPP
#include <unordered_map>
#include <string>
#include <mutex>
#include <stdexcept>

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

        // Adapter methods for compatibility with TradingEngine
        double getBalance() const { return getTotalBalance(); }
        void updateBalance(double amount) { addBalance(amount); }
        double getPosition(const std::string &symbol) const { return getTotalPosition(symbol); }
        void updatePosition(const std::string &symbol, double shares) {
            if (shares < 0) {
                // When removing shares, check if we have enough
                double current = getTotalPosition(symbol);
                if (current + shares < 0) {
                    throw std::runtime_error("Not enough shares to remove");
                }
            }
            // Add the shares (negative value means removing)
            positions[symbol] += shares;
            if (positions[symbol] <= 0) {
                positions.erase(symbol);
            }
        }
};

#endif //ACCOUNT_HPP
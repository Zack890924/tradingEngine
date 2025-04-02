#include <string>
#include <vector>
#include <ctime>
#ifndef ORDER_HPP
#define ORDER_HPP


enum class OrderSide {BUY, SELL};

struct OrderExecution {
    int id;
    double amount;
    double price;
    std::string executedAt;
};

enum class OrderStatus {
    OPEN,
    EXECUTED,
    CANCELED
};

class Record{
    private:
        int shares;
        double price;
        long timestamp;
    public:
        Record(int shares, double price, long timestamp);
        int getShares() const;
        double getPrice() const;
        long getTimestamp() const;
};


class Order{
    private:
        int order_id;
        std::string account_id;
        std::string symbol;
        double limit_price;
        int quantity;
        int filled;
        int cancelled;
        long cancelled_timestamp;
        long timestamp;
        OrderSide side;
        OrderStatus status;
        double open_amount;

        std::vector<Record> records;
    public:
        Order(int order_id, const std::string &account_id, const std::string &symbol, double limit_price, int quantity, long timestamp, OrderSide side);
        
        // Simplified constructor that automatically sets timestamp and side
        Order(int order_id, const std::string &account_id, const std::string &symbol, 
             double limit_price, int quantity)
            : order_id(order_id), account_id(account_id), symbol(symbol), 
              limit_price(limit_price), quantity(quantity), 
              filled(0), cancelled(0), cancelled_timestamp(0), 
              timestamp(std::time(nullptr)), // Current timestamp
              side(quantity > 0 ? OrderSide::BUY : OrderSide::SELL),
              status(OrderStatus::OPEN), open_amount(quantity) {}

        //getters
        int getOrderId() const;
        const std::string& getAccountId() const;
        const std::string& getSymbol() const;
        int getQuantity() const;
        int getFilled() const;
        int getCanceled() const;
        double getLimitPrice() const;
        long getTimestamp() const;
        const std::vector<Record> &getRecords() const;
        OrderSide getSide() const;
        long getCancelTime() const;
        int getOpenQuantity() const;
        double getOpenAmount() const;
        OrderStatus getStatus() const;
        double getAmount() const;

        //setters
        void setOpenAmount(double amount);
        void setStatus(OrderStatus status);

        // void addFilled(int quantity);
        void addCancel(int quantity);
        // void addRecord(const Record &record);
        void setCancelTime(long t);

        void addExecution(int qty, const Record &record);
        void reduceOpenQty(int qty);

        // Adapter methods for compatibility with TradingEngine
        int getOpenQty() const { return getOpenQuantity(); }
        int getQty() const { return getQuantity(); }

        // double getOpenAmount() const { 
        //     return side == OrderSide::BUY ? (quantity - filled - cancelled) : -(quantity - filled - cancelled); 
        // }
};

#endif //ORDER_HPP
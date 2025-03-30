#include <string>
#include <vector>
#ifndef ORDER_HPP
#define ORDER_HPP


enum class OrderSide {BUY, SELL};

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

        std::vector<Record> records;
    public:
        Order(int order_id, const std::string &account_id, const std::string &symbol, double limit_price, int quantity, long timestamp, OrderSide side);
        
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

        // void addFilled(int quantity);
        void addCancel(int quantity);
        // void addRecord(const Record &record);
        void setCancelTime(long t);


        void addExecution(int qty, const Record &record);
        
        
        
        

};



#endif //ORDER_HPP
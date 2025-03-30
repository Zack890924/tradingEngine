//src/order.cpp

#include "order.hpp"
#include <stdexcept>


Record::Record(int shares, double price, long timestamp) : shares(shares), price(price), timestamp(timestamp) {}

int Record::getShares() const {
    return shares;
}

double Record::getPrice() const {
    return price;
}

long Record::getTimestamp() const {
    return timestamp;
}

Order::Order(int order_id, const std::string &account_id, const std::string &symbol, double limit_price, 
            int quantity, long timestamp, OrderSide side) 
            : order_id(order_id), account_id(account_id), symbol(symbol), limit_price(limit_price),
             quantity(quantity), filled(0), cancelled(0), cancelled_timestamp(0), timestamp(timestamp), side(side) {}

int Order::getOrderId() const {
    return order_id;
}

const std::string& Order::getAccountId() const {
    return account_id;
}

const std::string& Order::getSymbol() const {
    return symbol;
}

int Order::getQuantity() const {
    return quantity;
}

int Order::getFilled() const {
    return filled;
}

int Order::getCanceled() const {
    return cancelled;
}

double Order::getLimitPrice() const {
    return limit_price;
}

long Order::getTimestamp() const {
    return timestamp;
}

OrderSide Order::getSide() const {
    return side;
}

long Order::getCancelTime() const {
    return cancelled_timestamp;
}

//order - deal - cancel
int Order::getOpenQuantity() const {
    return quantity - filled - cancelled;
}

const std::vector<Record> &Order::getRecords() const {
    return records;
}


// void Order::addFilled(int qty){
//     if(qty < 0){
//         throw std::runtime_error("Cannot add negative quantity");
//     }
//     if(filled + qty + cancelled > quantity){
//         throw std::runtime_error("Cannot fill more than the order quantity");
//     }

//     filled += quantity;
// }


void Order::addCancel(int qty){
    if(qty < 0){
        throw std::runtime_error("Cannot cancel negative quantity");
    }
    if(qty > quantity - filled - cancelled){
        throw std::runtime_error("Cannot cancel more than the open quantity");
    }
    cancelled += qty;
}

// void Order::addRecord(const Record &record){
//     if(record.getShares() < 0){
//         throw std::runtime_error("Shares cannot be negative");
//     }
//     if(getOpenQuantity() < record.getShares()){
//         throw std::runtime_error("Cannot add more shares than the open quantity");
//     }
//     records.push_back(record);
// }


void Order::setCancelTime(long t){
    cancelled_timestamp = t;
}


void Order::addExecution(int qty, const Record &record){
    if(record.getShares() < 0){
        throw std::runtime_error("Shares cannot be negative");
    } 

    if(getOpenQuantity() < record.getShares()){
        throw std::runtime_error("Cannot add more shares than the open quantity");
    }

    if(qty < 0){
        throw std::runtime_error("Cannot add negative quantity");
    }
    if(filled + qty + cancelled > quantity){
        throw std::runtime_error("Cannot fill more than the order quantity");
    }

    filled += qty;
    records.push_back(record);




}




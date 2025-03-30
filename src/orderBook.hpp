#ifndef ORDERBOOK_HPP
#define ORDERBOOK_HPP
#include "order.hpp"
#include <set>
#include <memory>

//price high to low 
struct BuyOrderCompare{
    bool operator()(const std::shared_ptr<Order> &a, const std::shared_ptr<Order> &b) const{
        if(a->getLimitPrice() != b->getLimitPrice()){
            return a->getLimitPrice() > b->getLimitPrice();
        }
        return a->getTimestamp() < b->getTimestamp();
    }
};

//price low to high
struct SellOrderCompare{
    bool operator()(const std::shared_ptr<Order> &a, const std::shared_ptr<Order> &b) const{
        if(a->getLimitPrice() != b->getLimitPrice()){
            return a->getLimitPrice() < b->getLimitPrice();
        }
        return a->getTimestamp() < b->getTimestamp();
    }
};



class OrderBook {
    public:
        std::multiset<std::shared_ptr<Order>, BuyOrderCompare> buyOrders;
        std::multiset<std::shared_ptr<Order>, SellOrderCompare> sellOrders;
        mutable std::mutex mtx;
        OrderBook() = default;
};


#endif //ORDERBOOK_HPP
#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include "order.hpp"
#include <memory>
#include <vector>
#include <map>
#include <string>

class OrderBook {
public:
    void addOrder(std::shared_ptr<Order> order);
    std::vector<std::shared_ptr<Order>> &getBuyOrders();
    std::vector<std::shared_ptr<Order>> &getSellOrders();
    std::shared_ptr<Order> findOrder(int orderId);
    std::shared_ptr<Order> findAndRemoveOrder(int orderId);
    void updateOrRemoveOrder(std::shared_ptr<Order> order);
    std::vector<std::shared_ptr<Order>> buyOrders;
    std::vector<std::shared_ptr<Order>> sellOrders;
    
    // Add other methods as needed
private:
    std::map<int, std::shared_ptr<Order>> ordersById;
    
    // Internal helpers
    void sortBuyOrders();
    void sortSellOrders();
};

#endif
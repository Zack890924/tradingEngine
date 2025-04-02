#include "orderBook.hpp"
#include <algorithm>

void OrderBook::addOrder(std::shared_ptr<Order> order) {
    ordersById[order->getOrderId()] = order;
    
    if (order->getSide() == OrderSide::BUY) {
        buyOrders.push_back(order);
        sortBuyOrders();
    } else {
        sellOrders.push_back(order);
        sortSellOrders();
    }
}

std::vector<std::shared_ptr<Order>>& OrderBook::getBuyOrders() {
    return buyOrders;
}

std::vector<std::shared_ptr<Order>>& OrderBook::getSellOrders() {
    return sellOrders;
}

std::shared_ptr<Order> OrderBook::findOrder(int orderId) {
    auto it = ordersById.find(orderId);
    if (it != ordersById.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<Order> OrderBook::findAndRemoveOrder(int orderId) {
    auto it = ordersById.find(orderId);
    if (it == ordersById.end()) {
        return nullptr;
    }
    
    std::shared_ptr<Order> order = it->second;
    ordersById.erase(it);
    
    // Remove from buy or sell orders
    if (order->getSide() == OrderSide::BUY) {
        auto buyIt = std::find(buyOrders.begin(), buyOrders.end(), order);
        if (buyIt != buyOrders.end()) {
            buyOrders.erase(buyIt);
        }
    } else {
        auto sellIt = std::find(sellOrders.begin(), sellOrders.end(), order);
        if (sellIt != sellOrders.end()) {
            sellOrders.erase(sellIt);
        }
    }
    
    return order;
}

void OrderBook::updateOrRemoveOrder(std::shared_ptr<Order> order) {
    if (order->getOpenQuantity() <= 0) {
        // Order is fully filled or cancelled, remove it
        findAndRemoveOrder(order->getOrderId());
    } else {
        // Resort the orders as the order's properties might have changed
        if (order->getSide() == OrderSide::BUY) {
            sortBuyOrders();
        } else {
            sortSellOrders();
        }
    }
}

void OrderBook::sortBuyOrders() {
    // Sort by price DESC (highest first), then by timestamp ASC (oldest first)
    std::sort(buyOrders.begin(), buyOrders.end(), 
              [](const std::shared_ptr<Order>& a, const std::shared_ptr<Order>& b) {
                  if (a->getLimitPrice() != b->getLimitPrice()) {
                      return a->getLimitPrice() > b->getLimitPrice();
                  }
                  return a->getTimestamp() < b->getTimestamp();
              });
}

void OrderBook::sortSellOrders() {
    // Sort by price ASC (lowest first), then by timestamp ASC (oldest first)
    std::sort(sellOrders.begin(), sellOrders.end(), 
              [](const std::shared_ptr<Order>& a, const std::shared_ptr<Order>& b) {
                  if (a->getLimitPrice() != b->getLimitPrice()) {
                      return a->getLimitPrice() < b->getLimitPrice();
                  }
                  return a->getTimestamp() < b->getTimestamp();
              });
}
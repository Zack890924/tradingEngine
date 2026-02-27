#ifndef ORDER_REPOSITORY_HPP
#define ORDER_REPOSITORY_HPP

#include "order.hpp"
#include <string>
#include <vector>
#include <memory>

// Forward declare pqxx::work
namespace pqxx {
    class work;
}

class OrderRepository {
public:
    // Create a new order and return its ID
    int createOrder(const std::string& accountId, const std::string& symbol,
                   double amount, double limitPrice);

    // Cancel an existing order
    bool cancelOrder(int orderId);

    // Get all open buy orders for a specific symbol (sorted by price DESC, time ASC)
    std::vector<std::shared_ptr<Order>> getOpenBuyOrders(const std::string& symbol);

    // Get all open sell orders for a specific symbol (sorted by price ASC, time ASC)
    std::vector<std::shared_ptr<Order>> getOpenSellOrders(const std::string& symbol);

    // Update an order's status and remaining open amount
    bool updateOrderStatus(int orderId, OrderStatus status, double openAmount);

    // Record an execution between two orders
    bool recordExecution(int buyOrderId, int sellOrderId,
                        const std::string& symbol, double amount, double price);

    // Get order by ID
    std::shared_ptr<Order> getOrder(int orderId);

    // Get all executions for an order
    std::vector<OrderExecution> getOrderExecutions(int orderId);

    // Transaction-aware versions (accept existing transaction)
    int createOrder(pqxx::work& txn, const std::string& accountId, const std::string& symbol,
                   double amount, double limitPrice);
    bool updateOrderStatus(pqxx::work& txn, int orderId, OrderStatus status, double openAmount);
    bool recordExecution(pqxx::work& txn, int buyOrderId, int sellOrderId,
                        const std::string& symbol, double amount, double price);
    std::shared_ptr<Order> getOrder(pqxx::work& txn, int orderId);
    bool cancelOrder(pqxx::work& txn, int orderId);
};

#endif
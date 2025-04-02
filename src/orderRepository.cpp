#include "orderRepository.hpp"
#include "dbConnection.hpp"
#include <pqxx/pqxx>
#include <memory>

int OrderRepository::createOrder(const std::string& accountId, const std::string& symbol, 
                               double amount, double limitPrice) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        
        pqxx::result r = txn.exec_params(
            "INSERT INTO orders (account_id, symbol, amount, limit_price, open_amount, status) "
            "VALUES ($1, $2, $3, $4, $5, $6) RETURNING order_id",
            accountId,
            symbol,
            amount,
            limitPrice,
            std::abs(amount), // open_amount starts as the full amount
            "OPEN"
        );
        
        int orderId = r[0][0].as<int>();
        txn.commit();
        return orderId;
    }
    catch (const std::exception& e) {
        // Log the error
        return -1;
    }
}

bool OrderRepository::cancelOrder(int orderId) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        
        pqxx::result r = txn.exec_params(
            "UPDATE orders SET status = 'CANCELED' WHERE order_id = $1 AND status = 'OPEN'",
            orderId
        );
        
        txn.commit();
        return r.affected_rows() > 0;
    }
    catch (const std::exception& e) {
        // Log the error
        return false;
    }
}

std::vector<std::shared_ptr<Order>> OrderRepository::getOpenBuyOrders(const std::string& symbol) {
    std::vector<std::shared_ptr<Order>> orders;
    try {
        pqxx::work txn(DBConnection::getConnection());
        
        pqxx::result r = txn.exec_params(
            "SELECT order_id, account_id, symbol, amount, limit_price, open_amount, created_at "
            "FROM orders "
            "WHERE symbol = $1 AND status = 'OPEN' AND amount > 0 "
            "ORDER BY limit_price DESC, created_at ASC",
            symbol
        );
        
        for (const auto& row : r) {
            int orderId = row["order_id"].as<int>();
            std::string accountId = row["account_id"].as<std::string>();
            std::string sym = row["symbol"].as<std::string>();
            double amt = row["amount"].as<double>();
            double limitPrice = row["limit_price"].as<double>();
            
            // Create Order object according to your actual Order constructor
            // This is a placeholder - adjust to match your Order class constructor
            auto order = std::make_shared<Order>(orderId, accountId, sym, amt, limitPrice);
            orders.push_back(order);
        }
    }
    catch (const std::exception& e) {
        // Log the error
    }
    return orders;
}

std::vector<std::shared_ptr<Order>> OrderRepository::getOpenSellOrders(const std::string& symbol) {
    std::vector<std::shared_ptr<Order>> orders;
    try {
        pqxx::work txn(DBConnection::getConnection());
        
        pqxx::result r = txn.exec_params(
            "SELECT order_id, account_id, symbol, amount, limit_price, open_amount, created_at "
            "FROM orders "
            "WHERE symbol = $1 AND status = 'OPEN' AND amount < 0 "
            "ORDER BY limit_price ASC, created_at ASC",
            symbol
        );
        
        for (const auto& row : r) {
            int orderId = row["order_id"].as<int>();
            std::string accountId = row["account_id"].as<std::string>();
            std::string sym = row["symbol"].as<std::string>();
            double amt = row["amount"].as<double>();
            double limitPrice = row["limit_price"].as<double>();
            
            // Create Order object according to your actual Order constructor
            auto order = std::make_shared<Order>(orderId, accountId, sym, amt, limitPrice);
            orders.push_back(order);
        }
    }
    catch (const std::exception& e) {
        // Log the error
    }
    return orders;
}

bool OrderRepository::updateOrderStatus(int orderId, OrderStatus status, double openAmount) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        
        std::string statusStr;
        switch(status) {
            case OrderStatus::OPEN: 
                statusStr = "OPEN"; 
                break;
            case OrderStatus::EXECUTED: 
                statusStr = "EXECUTED"; 
                break;
            case OrderStatus::CANCELED: 
                statusStr = "CANCELED"; 
                break;
        }
        
        pqxx::result r = txn.exec_params(
            "UPDATE orders SET status = $1, open_amount = $2 WHERE order_id = $3",
            statusStr,
            openAmount,
            orderId
        );
        
        txn.commit();
        return r.affected_rows() > 0;
    }
    catch (const std::exception& e) {
        // Log the error
        return false;
    }
}

bool OrderRepository::recordExecution(int buyOrderId, int sellOrderId, 
                                    const std::string& symbol, double amount, double price) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        
        txn.exec_params(
            "INSERT INTO executions (buy_order_id, sell_order_id, symbol, amount, price) "
            "VALUES ($1, $2, $3, $4, $5)",
            buyOrderId,
            sellOrderId,
            symbol,
            amount,
            price
        );
        
        txn.commit();
        return true;
    }
    catch (const std::exception& e) {
        // Log the error
        return false;
    }
}

std::shared_ptr<Order> OrderRepository::getOrder(int orderId) {
    std::shared_ptr<Order> order;
    try {
        pqxx::work txn(DBConnection::getConnection());
        
        pqxx::result r = txn.exec_params(
            "SELECT order_id, account_id, symbol, amount, limit_price, open_amount, status, created_at "
            "FROM orders "
            "WHERE order_id = $1",
            orderId
        );
        
        if (!r.empty()) {
            const auto& row = r[0];
            int id = row["order_id"].as<int>();
            std::string accountId = row["account_id"].as<std::string>();
            std::string sym = row["symbol"].as<std::string>();
            double amt = row["amount"].as<double>();
            double limitPrice = row["limit_price"].as<double>();
            
            // Create Order object according to your actual Order constructor
            order = std::make_shared<Order>(id, accountId, sym, amt, limitPrice);
        }
    }
    catch (const std::exception& e) {
        // Log the error
    }
    return order;
}

std::vector<OrderExecution> OrderRepository::getOrderExecutions(int orderId) {
    std::vector<OrderExecution> executions;
    try {
        pqxx::work txn(DBConnection::getConnection());
        
        pqxx::result r = txn.exec_params(
            "SELECT execution_id, amount, price, executed_at "
            "FROM executions "
            "WHERE buy_order_id = $1 OR sell_order_id = $1",
            orderId
        );
        
        for (const auto& row : r) {
            OrderExecution execution;
            execution.id = row["execution_id"].as<int>();
            execution.amount = row["amount"].as<double>();
            execution.price = row["price"].as<double>();
            execution.executedAt = row["executed_at"].as<std::string>();
            executions.push_back(execution);
        }
    }
    catch (const std::exception& e) {
        // Log the error
    }
    return executions;
}
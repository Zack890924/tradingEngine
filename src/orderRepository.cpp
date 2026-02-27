#include "orderRepository.hpp"
#include "dbConnection.hpp"
#include <pqxx/pqxx>
#include <memory>
#include <iostream>

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
            std::abs(amount),
            "OPEN"
        );
        int orderId = r[0][0].as<int>();
        txn.commit();
        return orderId;
    }
    catch (const std::exception& e) {
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
            double limitP = row["limit_price"].as<double>();
            OrderSide side = OrderSide::BUY;
            int quantity = std::abs(amt);
            long createdTs = 0;

            auto o = std::make_shared<Order>(
                orderId,
                accountId,
                sym,
                limitP,        
                quantity,     
                createdTs,     // timestamp
                side           
            );
            orders.push_back(o);
        }
    }
    catch (const std::exception& e) {
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
            double limitP = row["limit_price"].as<double>();
            OrderSide side = OrderSide::SELL;
            int quantity = std::abs(amt);
            long createdTs = 0;

            auto o = std::make_shared<Order>(
                orderId,
                accountId,
                sym,
                limitP,
                quantity,
                createdTs,
                side
            );

            orders.push_back(o);
        }
    }
    catch (const std::exception& e) {
    }
    return orders;
}

bool OrderRepository::updateOrderStatus(int orderId, OrderStatus status, double openAmount) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        std::string s;
        switch(status) {
            case OrderStatus::OPEN: 
                s = "OPEN"; 
                break;
            case OrderStatus::EXECUTED: 
                s = "EXECUTED"; 
                break;
            case OrderStatus::CANCELED: 
                s = "CANCELED"; 
                break;
        }
        pqxx::result r = txn.exec_params(
            "UPDATE orders SET status = $1, open_amount = $2 WHERE order_id = $3",
            s,
            openAmount,
            orderId
        );
        txn.commit();
        return r.affected_rows() > 0;
    }
    catch (const std::exception& e) {
        return false;
    }
}

bool OrderRepository::recordExecution(int buyOrderId, int sellOrderId, const std::string& symbol, double amount, double price) {
    try {
        pqxx::work txn(DBConnection::getConnection());
        txn.exec_params(
            "INSERT INTO executions (buy_order_id, sell_order_id, symbol, amount, price, executed_at) VALUES ($1, $2, $3, $4, $5, NOW())",
            buyOrderId, sellOrderId, symbol, amount, price
        );
        txn.commit();
        std::cout << "Execution recorded: buy=" << buyOrderId << ", sell=" << sellOrderId << ", amount=" << amount << ", price=" << price << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error inserting execution: " << e.what() << std::endl;
        return false;
    }
}

std::shared_ptr<Order> OrderRepository::getOrder(int orderId) {
    std::shared_ptr<Order> o;
    try {
        pqxx::work txn(DBConnection::getConnection());
        pqxx::result r = txn.exec_params(
            "SELECT order_id, account_id, symbol, amount, limit_price, open_amount, status, created_at FROM orders WHERE order_id = $1",
            orderId
        );
        if (!r.empty()) {
            const auto& row = r[0];
            int id = row["order_id"].as<int>();
            std::string acc = row["account_id"].as<std::string>();
            std::string sym = row["symbol"].as<std::string>();
            double amt = row["amount"].as<double>();
            double lp = row["limit_price"].as<double>();
            double openAmt = row["open_amount"].as<double>();
            std::string statusStr = row["status"].as<std::string>();
            long createdTs = 0;  
        
            OrderSide side = amt > 0 ? OrderSide::BUY : OrderSide::SELL;
            int quantity = std::abs((int)amt);
        
            o = std::make_shared<Order>(id, acc, sym, lp, quantity, createdTs, side);
        
            o->setOpenAmount(openAmt); 
        
            if (statusStr == "OPEN") o->setStatus(OrderStatus::OPEN);
            else if (statusStr == "EXECUTED") o->setStatus(OrderStatus::EXECUTED);
            else if (statusStr == "CANCELED") o->setStatus(OrderStatus::CANCELED);
        }
        
    }
    catch (const std::exception& e) {
    }
    return o;
}

std::vector<OrderExecution> OrderRepository::getOrderExecutions(int orderId) {
    std::vector<OrderExecution> v;
    try {
        pqxx::work txn(DBConnection::getConnection());
        pqxx::result r = txn.exec_params(
            "SELECT execution_id, amount, price, executed_at FROM executions WHERE buy_order_id = $1 OR sell_order_id = $1",
            orderId
        );
        for(const auto& row : r) {
            OrderExecution execution;
            execution.id = row["execution_id"].as<int>();
            execution.amount = row["amount"].as<double>();
            execution.price = row["price"].as<double>();
            execution.executedAt = row["executed_at"].as<std::string>();
            v.push_back(execution);
        }
    }
    catch (const std::exception& e) {
    }
    return v;
}

// Transaction-aware versions
int OrderRepository::createOrder(pqxx::work& txn, const std::string& accountId, const std::string& symbol,
                               double amount, double limitPrice) {
    try {
        pqxx::result r = txn.exec_params(
            "INSERT INTO orders (account_id, symbol, amount, limit_price, open_amount, status) "
            "VALUES ($1, $2, $3, $4, $5, $6) RETURNING order_id",
            accountId,
            symbol,
            amount,
            limitPrice,
            std::abs(amount),
            "OPEN"
        );
        int orderId = r[0][0].as<int>();
        return orderId;
    }
    catch (const std::exception& e) {
        return -1;
    }
}

bool OrderRepository::updateOrderStatus(pqxx::work& txn, int orderId, OrderStatus status, double openAmount) {
    try {
        std::string s;
        switch(status) {
            case OrderStatus::OPEN:
                s = "OPEN";
                break;
            case OrderStatus::EXECUTED:
                s = "EXECUTED";
                break;
            case OrderStatus::CANCELED:
                s = "CANCELED";
                break;
        }
        pqxx::result r = txn.exec_params(
            "UPDATE orders SET status = $1, open_amount = $2 WHERE order_id = $3",
            s,
            openAmount,
            orderId
        );
        return r.affected_rows() > 0;
    }
    catch (const std::exception& e) {
        return false;
    }
}

bool OrderRepository::recordExecution(pqxx::work& txn, int buyOrderId, int sellOrderId,
                                     const std::string& symbol, double amount, double price) {
    try {
        txn.exec_params(
            "INSERT INTO executions (buy_order_id, sell_order_id, symbol, amount, price, executed_at) VALUES ($1, $2, $3, $4, $5, NOW())",
            buyOrderId, sellOrderId, symbol, amount, price
        );
        std::cout << "Execution recorded: buy=" << buyOrderId << ", sell=" << sellOrderId << ", amount=" << amount << ", price=" << price << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error inserting execution: " << e.what() << std::endl;
        return false;
    }
}

std::shared_ptr<Order> OrderRepository::getOrder(pqxx::work& txn, int orderId) {
    std::shared_ptr<Order> o;
    try {
        pqxx::result r = txn.exec_params(
            "SELECT order_id, account_id, symbol, amount, limit_price, open_amount, status, created_at FROM orders WHERE order_id = $1",
            orderId
        );
        if (!r.empty()) {
            const auto& row = r[0];
            int id = row["order_id"].as<int>();
            std::string acc = row["account_id"].as<std::string>();
            std::string sym = row["symbol"].as<std::string>();
            double amt = row["amount"].as<double>();
            double lp = row["limit_price"].as<double>();
            double openAmt = row["open_amount"].as<double>();
            std::string statusStr = row["status"].as<std::string>();
            long createdTs = 0;

            OrderSide side = amt > 0 ? OrderSide::BUY : OrderSide::SELL;
            int quantity = std::abs((int)amt);

            o = std::make_shared<Order>(id, acc, sym, lp, quantity, createdTs, side);

            o->setOpenAmount(openAmt);

            if (statusStr == "OPEN") o->setStatus(OrderStatus::OPEN);
            else if (statusStr == "EXECUTED") o->setStatus(OrderStatus::EXECUTED);
            else if (statusStr == "CANCELED") o->setStatus(OrderStatus::CANCELED);
        }

    }
    catch (const std::exception& e) {
    }
    return o;
}

bool OrderRepository::cancelOrder(pqxx::work& txn, int orderId) {
    try {
        pqxx::result r = txn.exec_params(
            "UPDATE orders SET status = 'CANCELED' WHERE order_id = $1 AND status = 'OPEN'",
            orderId
        );
        return r.affected_rows() > 0;
    }
    catch (const std::exception& e) {
        return false;
    }
}

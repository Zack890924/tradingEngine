#ifndef TRADING_ENGINE_HPP
#define TRADING_ENGINE_HPP

#include "account.hpp"
#include "order.hpp"
#include "orderBook.hpp"
#include "accountRepository.hpp"
#include "orderRepository.hpp"
#include "tinyxml2.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>

// Combined TradingEngine header integrating version 1 and version 2

class TradingEngine {
private:
    // Database repositories
    AccountRepository accountRepo;
    OrderRepository orderRepo;
    
    // In-memory data structures (for backward compatibility)
    std::unordered_map<std::string, Account> accounts;
    std::unordered_map<int, std::shared_ptr<Order>> orders;
    std::unordered_map<std::string, OrderBook> orderBooks;
    std::mutex accountsMtx;
    std::mutex ordersMtx;
    std::mutex orderBooksMtx;
    std::mutex globalMtx;
    std::atomic<long long> orderCounter;
    
    // Helper methods
    void logError(const std::string &message);
    OrderBook& getOrCreateOrderBook(const std::string &symbol);
    OrderBook* getOrderBookIfExist(const std::string &symbol);
    void trade(std::shared_ptr<Order> buyOrder, std::shared_ptr<Order> sellOrder, int qty, double tradePrice);
    
    // XML processing methods
    void processCreate(const tinyxml2::XMLElement *root,
                       tinyxml2::XMLElement *results,
                       tinyxml2::XMLDocument *respDoc);
    void processTransaction(const tinyxml2::XMLElement *root,
                            tinyxml2::XMLElement *results);
    tinyxml2::XMLElement* buildOrderElement(tinyxml2::XMLDocument *doc,
                                             std::shared_ptr<Order> order);
    
    std::shared_ptr<Order> getOrderById(int order_id);
    void processQuery(const tinyxml2::XMLElement *queryElem,
                      tinyxml2::XMLElement *results,
                      const std::string &accountId);
    void processCancel(const tinyxml2::XMLElement *cancelElem,
                       tinyxml2::XMLElement *results,
                       const std::string &accountId);
    
public:
    TradingEngine();
    
    // Original account operations
    bool createAccount(const std::string &account_id, double balance, std::string &msg);
    bool createSymbol(const std::string &account_id, const std::string &symbol, double shares, std::string &msg);
    
    // Original order operations
    bool placeOrder(const std::string &account_id, const std::string &symbol, 
                    double limit_price, int quantity, OrderSide side, 
                    std::string &msg, int &createdOrderId);
    bool cancelOrder(int order_id, std::string &msg);
    bool queryOrder(int order_id, std::string &XML_info);
    
    // Database-backed operations
    bool createAccount(const std::string& accountId, double balance);
    bool addSymbolToAccount(const std::string& symbol, const std::string& accountId, double amount);
    int placeOrder(const std::string& accountId, const std::string& symbol, 
                   double amount, double limitPrice);
    bool cancelOrder(int orderId);
    std::vector<OrderStatus> queryOrder(int orderId);
    
    // Order matching and execution
    void matchOrders(const std::string &symbol);
    
    // Transaction execution within a database transaction
    bool executeTransaction(const Order& buyOrder, const Order& sellOrder, 
                            double amount, double price);
    
    // Process XML requests
    std::string processRequest(const std::string &xmlStr);
    
    // Additional XML processing method
    void processOrder(const tinyxml2::XMLElement *orderElem,
                      tinyxml2::XMLElement *results,
                      const std::string &accountId);
};

#endif

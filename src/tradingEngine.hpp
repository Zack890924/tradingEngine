#ifndef TRADINGENGINE_HPP
#define TRADINGENGINE_HPP

#include "order.hpp"
#include "account.hpp"
#include "orderBook.hpp"
#include "tinyxml2.h"
#include <memory>
#include <atomic>



class TradingEngine {

    public:
        TradingEngine();
        bool createAccount(const std::string &account_id, double balance, std::string &msg);
        bool createSymbol(const std::string &account_id, const std::string &symbol, double shares, std::string &msg);
        bool cancelOrder(int order_id, std::string &msg);
        bool placeOrder(const std::string &account_id, const std::string &symbol, double limit_price, int quantity, OrderSide side, std::string &msg, int &createdOrderId);
        bool queryOrder(int order_id, std::string &XML_info);

        std::string processRequest(const std::string &xmlStr);
    private:
        // std::mutex globalMtx;
        std::mutex accountsMtx;         
        std::mutex ordersMtx;          
        std::mutex orderBooksMtx;


        std::unordered_map<std::string, Account> accounts;
        std::unordered_map<int, std::shared_ptr<Order>> orders;
        std::unordered_map<std::string, OrderBook> orderBooks;

        void processCreate(const tinyxml2::XMLElement *root,
            tinyxml2::XMLElement *results,
            tinyxml2::XMLDocument *respDoc);


        void processTransaction(const tinyxml2::XMLElement *root, tinyxml2::XMLElement *results);

        tinyxml2::XMLElement *buildOrderElement(tinyxml2::XMLDocument *doc, std::shared_ptr<Order> order);




        std::atomic<long long> orderCounter;

        void logError(const std::string &message);
        void matchOrders(const std::string &symbol);
        OrderBook& getOrCreateOrderBook(const std::string &symbol);
        OrderBook* getOrderBookIfExist(const std::string &symbol);
        void trade(std::shared_ptr<Order> buyOrder, std::shared_ptr<Order> sellOrder, int qty, double tradePrice);

        
};





#endif //TRADIKNGENGINE_HPP
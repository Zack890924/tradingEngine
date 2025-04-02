#include "tradingEngine.hpp"
#include <iostream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <cassert>
#include <cstring>

// Include tinyxml2 namespace at global level
using namespace tinyxml2;

TradingEngine::TradingEngine() : orderCounter(1) {
    // Initialize any database-specific setup if needed
}

void TradingEngine::logError(const std::string &message) {
    std::cerr << "[Error] " << message << std::endl;
}

// Original in-memory implementation
bool TradingEngine::createAccount(const std::string &account_id, double balance, std::string &msg){
    std::lock_guard<std::mutex> lock(accountsMtx);
    auto it = accounts.find(account_id);
    if(it != accounts.end()){
        msg = "Account already exists";
        return false;
    }
    accounts.try_emplace(account_id, account_id, balance);
    msg = "Account created";
    
    // Also use the database
    accountRepo.createAccount(account_id, balance);
    
    return true;
}

// Database-backed implementation
bool TradingEngine::createAccount(const std::string& accountId, double balance) {
    return accountRepo.createAccount(accountId, balance);
}

bool TradingEngine::createSymbol(const std::string &account_id, const std::string &symbol, double shares, std::string &msg){
    std::lock_guard<std::mutex> lock(accountsMtx);
    auto accIt = accounts.find(account_id);
    if(accIt == accounts.end()){
        msg = "Account does not exist";
        return false;
    }

    Account &acc = accIt->second;
    acc.addPosition(symbol, shares);
    msg = "Symbol created";
    
    // Also use the database
    accountRepo.addPosition(account_id, symbol, shares);
    
    return true;
}

bool TradingEngine::addSymbolToAccount(const std::string& symbol, const std::string& accountId, double amount) {
    return accountRepo.addPosition(accountId, symbol, amount);
}

OrderBook &TradingEngine::getOrCreateOrderBook(const std::string &symbol){
    std::lock_guard<std::mutex> lock(orderBooksMtx);
    auto it = orderBooks.find(symbol);
    if(it == orderBooks.end()){
        orderBooks.try_emplace(symbol);
    }
    return orderBooks[symbol];
}

OrderBook *TradingEngine::getOrderBookIfExist(const std::string &symbol){
    std::lock_guard<std::mutex> lock(orderBooksMtx);
    auto it = orderBooks.find(symbol);
    if(it != orderBooks.end()){
        return &(it->second);
    }
    return nullptr;
}

void TradingEngine::matchOrders(const std::string& symbol){
    OrderBook &book = getOrCreateOrderBook(symbol);
    
    while(true){
        // Check if we have matching orders
        if(book.getBuyOrders().empty() || book.getSellOrders().empty()){
            break;
        }

        std::shared_ptr<Order> buyOrder = book.getBuyOrders().front();
        std::shared_ptr<Order> sellOrder = book.getSellOrders().front();

        if(buyOrder->getLimitPrice() < sellOrder->getLimitPrice()){
            break; // Highest buy price is less than lowest sell price, no match
        }

        // Match found
        int tradeQty = std::min(buyOrder->getOpenQuantity(), sellOrder->getOpenQuantity());
        double tradePrice = (buyOrder->getTimestamp() < sellOrder->getTimestamp()) ? 
                            buyOrder->getLimitPrice() : sellOrder->getLimitPrice();

        try {
            trade(buyOrder, sellOrder, tradeQty, tradePrice);
            
            // Also use the database implementation
            if (buyOrder->getOrderId() > 0 && sellOrder->getOrderId() > 0) {
                executeTransaction(*buyOrder, *sellOrder, tradeQty, tradePrice);
            }
        }
        catch(const std::exception &e){
            logError("Trade failed :" + std::string(e.what()));
            break;
        }

        // Check if orders are filled and should be removed
        book.updateOrRemoveOrder(buyOrder);
        book.updateOrRemoveOrder(sellOrder);
    }
}

bool TradingEngine::placeOrder(const std::string &account_id, const std::string &symbol, double limit_price, int quantity, OrderSide side, std::string &msg, int &createdOrderId)
{
    {
        std::lock_guard<std::mutex> lock(accountsMtx);
        auto accIt = accounts.find(account_id);
        if(accIt == accounts.end()){
            msg = "Account does not exist";
            return false;
        }

        Account &acc = accIt->second;

        if(side == OrderSide::BUY){
            double cost = quantity * limit_price;
            if(acc.getBalance() < cost){
                msg = "Insufficient funds";
                return false;
            }
            acc.updateBalance(-cost);
        } else {
            double shares = acc.getPosition(symbol);
            if(shares < quantity){
                msg = "Insufficient shares";
                return false;
            }
            acc.updatePosition(symbol, -quantity);
        }
    }

    { // Create and add order
        std::lock_guard<std::mutex> lock(globalMtx);
        createdOrderId = orderCounter++;
    }

    // Create order and add to order book
    OrderBook &book = getOrCreateOrderBook(symbol);
    int amt = (side == OrderSide::BUY) ? quantity : -quantity;
    std::shared_ptr<Order> order = std::make_shared<Order>(createdOrderId, account_id, symbol, amt, limit_price);
    
    // Add order to the order book
    book.addOrder(order);

    // Try to match orders immediately
    matchOrders(symbol);

    msg = "Order placed successfully";
    return true;
}

// Database-backed implementation
int TradingEngine::placeOrder(const std::string& accountId, const std::string& symbol, 
                            double amount, double limitPrice) {
    // Validate account exists
    if (!accountRepo.accountExists(accountId)) {
        return -1; // Account doesn't exist
    }

    // For buy orders, check if enough balance
    if (amount > 0) { // Buy order
        double cost = amount * limitPrice;
        double balance = accountRepo.getBalance(accountId);
        
        if (balance < cost) {
            return -1; // Insufficient funds
        }
        
        // Deduct the cost from account balance
        if (!accountRepo.updateBalance(accountId, balance - cost)) {
            return -1; // Failed to update balance
        }
    } else { // Sell order
        // Check if enough shares to sell
        double shares = accountRepo.getPosition(accountId, symbol);
        if (shares < std::abs(amount)) {
            return -1; // Insufficient shares
        }
        
        // Deduct shares from account
        if (!accountRepo.updatePosition(accountId, symbol, shares - std::abs(amount))) {
            return -1; // Failed to update position
        }
    }
    
    // Create the order
    int orderId = orderRepo.createOrder(accountId, symbol, amount, limitPrice);
    
    // Try to match the order immediately
    if (orderId > 0) {
        matchOrders(symbol);
    }
    
    return orderId;
}

void TradingEngine::trade(std::shared_ptr<Order> buyOrder, std::shared_ptr<Order> sellOrder, int qty, double tradePrice){
    std::lock_guard<std::mutex> lock(accountsMtx);
    
    // Check if accounts exist
    auto buyerIt = accounts.find(buyOrder->getAccountId());
    auto sellerIt = accounts.find(sellOrder->getAccountId());
    
    if(buyerIt == accounts.end() || sellerIt == accounts.end()){
        throw std::runtime_error("Account not found");
    }
    
    // Update buyer account (add shares)
    Account &buyer = buyerIt->second;
    buyer.updatePosition(buyOrder->getSymbol(), qty);
    
    // Update seller account (add money)
    Account &seller = sellerIt->second;
    double amount = qty * tradePrice;
    seller.updateBalance(amount);
    
    // Update order quantities
    buyOrder->reduceOpenQty(qty);
    sellOrder->reduceOpenQty(qty);
}

bool TradingEngine::executeTransaction(const Order& buyOrder, const Order& sellOrder, 
                                     double amount, double price) {
    try {
        // Implement database-backed transaction
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error executing transaction: " << e.what() << std::endl;
        return false;
    }
}

bool TradingEngine::cancelOrder(int order_id, std::string &msg){
    for(auto &[symbol, book] : orderBooks){
        std::shared_ptr<Order> order = book.findAndRemoveOrder(order_id);
        if(order){
            std::lock_guard<std::mutex> lock(accountsMtx);
            auto accIt = accounts.find(order->getAccountId());
            if(accIt == accounts.end()){
                msg = "Account not found";
                return false;
            }
            
            Account &acc = accIt->second;
            if(order->getOpenQty() > 0){ // If there are still open shares
                if(order->getSide() == OrderSide::BUY){
                    // Refund money for unfilled shares
                    double refund = order->getOpenQty() * order->getLimitPrice();
                    acc.updateBalance(refund);
                } else {
                    // Return shares for unfilled order
                    acc.updatePosition(order->getSymbol(), order->getOpenQty());
                }
            }
            
            msg = "Order canceled";
            return true;
        }
    }
    msg = "Order not found";
    return false;
}

// Database-backed implementation
bool TradingEngine::cancelOrder(int orderId) {
    auto order = orderRepo.getOrder(orderId);
    if (!order) {
        return false;
    }
    
    bool canceled = orderRepo.cancelOrder(orderId);
    if (canceled) {
        // If it was a buy order, refund the money
        if (order->getAmount() > 0) {
            double refundAmount = order->getOpenAmount() * order->getLimitPrice();
            double currentBalance = accountRepo.getBalance(order->getAccountId());
            accountRepo.updateBalance(order->getAccountId(), currentBalance + refundAmount);
        } else {
            // If it was a sell order, return the shares
            double returnShares = std::abs(order->getOpenAmount());
            double currentShares = accountRepo.getPosition(order->getAccountId(), order->getSymbol());
            accountRepo.updatePosition(order->getAccountId(), order->getSymbol(), currentShares + returnShares);
        }
    }
    
    return canceled;
}

bool TradingEngine::queryOrder(int order_id, std::string &XML_info){
    for(auto &[symbol, book] : orderBooks){
        std::shared_ptr<Order> order = book.findOrder(order_id);
        if(order){
            XML_info = "<status id=\"" + std::to_string(order_id) + "\">";
            
            // Add open tag if there are open shares
            if(order->getOpenQty() > 0){
                XML_info += "<open shares=\"" + std::to_string(order->getOpenQty()) + "\"/>";
            }
            
            // Add executed tag if some shares were executed
            int executedQty = order->getQty() - order->getOpenQty();
            if(executedQty > 0){
                XML_info += "<executed shares=\"" + std::to_string(executedQty) + 
                           "\" price=\"" + std::to_string(order->getLimitPrice()) + 
                           "\" time=\"" + std::to_string(order->getTimestamp()) + "\"/>";
            }
            
            XML_info += "</status>";
            return true;
        }
    }
    
    XML_info = "<status id=\"" + std::to_string(order_id) + "\">";
    XML_info += "<error>Order not found</error>";
    XML_info += "</status>";
    return false;
}

// Database-backed implementation
std::vector<OrderStatus> TradingEngine::queryOrder(int orderId) {
    std::vector<OrderStatus> statuses;
    auto order = orderRepo.getOrder(orderId);
    if (!order) {
        return statuses; // Order not found
    }
    
    // Get executions for the order
    auto executions = orderRepo.getOrderExecutions(orderId);
    
    // Add execution statuses
    for (const auto& execution : executions) {
        statuses.push_back(OrderStatus::EXECUTED);
    }
    
    // Add open status if order is still open
    if (order->getStatus() == OrderStatus::OPEN && order->getOpenAmount() > 0) {
        statuses.push_back(OrderStatus::OPEN);
    }
    
    // Add canceled status if order was canceled
    if (order->getStatus() == OrderStatus::CANCELED) {
        statuses.push_back(OrderStatus::CANCELED);
    }
    
    return statuses;
}

std::string TradingEngine::processRequest(const std::string &xmlStr){
    using namespace tinyxml2;
    
    // Prepare the response document
    XMLDocument respDoc;
    XMLElement *rootResp = respDoc.NewElement("results");
    respDoc.InsertFirstChild(rootResp);
    
    // Parse the input XML
    XMLDocument doc;
    XMLError err = doc.Parse(xmlStr.c_str());
    if (err != XML_SUCCESS) {
        XMLElement *e = respDoc.NewElement("error");
        rootResp->InsertEndChild(e);
        e->SetText(doc.ErrorStr());
        
        // Convert to string and return
        XMLPrinter printer;
        respDoc.Print(&printer);
        return printer.CStr();
    }
    
    // Get the root element
    XMLElement *root = doc.RootElement();
    if (!root) {
        XMLElement *er = respDoc.NewElement("error");
        rootResp->InsertEndChild(er);
        er->SetText("Invalid XML: missing root element");
        
        // Convert to string and return
        XMLPrinter printer;
        respDoc.Print(&printer);
        return printer.CStr();
    }
    
    const char* rootName = root->Name();
    if (!rootName) {
        XMLElement *er = respDoc.NewElement("error");
        rootResp->InsertEndChild(er);
        er->SetText("Invalid XML: root element has no name");
        
        // Convert to string and return
        XMLPrinter printer;
        respDoc.Print(&printer);
        return printer.CStr();
    }
    
    if(strcmp(rootName, "create") == 0){
        processCreate(root, rootResp, &respDoc);
    }
    else if (strcmp(rootName, "transactions") == 0) {
        processTransaction(root, rootResp);
    }
    else {
        XMLElement *er = respDoc.NewElement("error");
        rootResp->InsertEndChild(er);
        er->SetText("Invalid root element name. Expected 'create' or 'transactions'");
    }
    
    // Convert to string and return
    XMLPrinter printer;
    respDoc.Print(&printer);
    return printer.CStr();
}

// Add stub implementations for processCreate, processTransaction, and buildOrderElement
void TradingEngine::processCreate(const tinyxml2::XMLElement *root, tinyxml2::XMLElement *results, tinyxml2::XMLDocument *respDoc) {
    // Implement as needed
}

tinyxml2::XMLElement* TradingEngine::buildOrderElement(tinyxml2::XMLDocument *doc, std::shared_ptr<Order> order) {
    // Implement as needed
    return nullptr;
}

void TradingEngine::processTransaction(const tinyxml2::XMLElement *root, tinyxml2::XMLElement *results) {
    // Implement as needed
}


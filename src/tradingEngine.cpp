#include "tradingEngine.hpp"
#include <iostream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <cassert>
#include <cstring>
#include "tinyxml2.h"

using namespace tinyxml2;

TradingEngine::TradingEngine() : orderCounter(1) {
    // Initialize any database-specific setup if needed
}

void TradingEngine::logError(const std::string &message) {
    std::cerr << "[Error] " << message << std::endl;
}

bool TradingEngine::createAccount(const std::string &account_id, double balance, std::string &msg) {
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

bool TradingEngine::createSymbol(const std::string &account_id, const std::string &symbol, double shares, std::string &msg) {
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

OrderBook &TradingEngine::getOrCreateOrderBook(const std::string &symbol) {
    std::lock_guard<std::mutex> lock(orderBooksMtx);
    auto it = orderBooks.find(symbol);
    if(it == orderBooks.end()){
        orderBooks.try_emplace(symbol);
    }
    return orderBooks[symbol];
}

OrderBook *TradingEngine::getOrderBookIfExist(const std::string &symbol) {
    std::lock_guard<std::mutex> lock(orderBooksMtx);
    auto it = orderBooks.find(symbol);
    if(it != orderBooks.end()){
        return &(it->second);
    }
    return nullptr;
}

void TradingEngine::matchOrders(const std::string &symbol) {
    OrderBook &book = getOrCreateOrderBook(symbol);
    
    while(true) {
        if(book.getBuyOrders().empty() || book.getSellOrders().empty()){
            break;
        }
        std::shared_ptr<Order> buyOrder = book.getBuyOrders().front();
        std::shared_ptr<Order> sellOrder = book.getSellOrders().front();
        if(buyOrder->getLimitPrice() < sellOrder->getLimitPrice()){
            break;
        }
        int tradeQty = std::min(buyOrder->getOpenQuantity(), sellOrder->getOpenQuantity());
        double tradePrice = (buyOrder->getTimestamp() < sellOrder->getTimestamp()) ?
                            buyOrder->getLimitPrice() : sellOrder->getLimitPrice();
        try {
            trade(buyOrder, sellOrder, tradeQty, tradePrice);
            if(buyOrder->getOrderId() > 0 && sellOrder->getOrderId() > 0) {
                executeTransaction(*buyOrder, *sellOrder, tradeQty, tradePrice);
            }
        }
        catch(const std::exception &e) {
            logError("Trade failed :" + std::string(e.what()));
            break;
        }
        book.updateOrRemoveOrder(buyOrder);
        book.updateOrRemoveOrder(sellOrder);
    }
}

int TradingEngine::placeOrder(const std::string &accountId, const std::string &symbol, 
                                double amount, double limitPrice) {
    std::cout << "==== PLACING ORDER ====" << std::endl;
    std::cout << "Account: " << accountId << ", Symbol: " << symbol 
              << ", Amount: " << amount << ", Limit: " << limitPrice << std::endl;
    
    try {
        if(!accountRepo.accountExists(accountId)) {
            std::cerr << "ERROR: Account does not exist: " << accountId << std::endl;
            return -1;
        }
        if(amount > 0) {
            double cost = amount * limitPrice;
            double balance = accountRepo.getBalance(accountId);
            std::cout << "Buy order - Cost: " << cost << ", Available balance: " << balance << std::endl;
            if(balance < cost) {
                std::cerr << "ERROR: Insufficient balance for buy order" << std::endl;
                return -1;
            }
            if(!accountRepo.updateBalance(accountId, balance - cost)) {
                std::cerr << "ERROR: Failed to update balance" << std::endl;
                return -1;
            }
            std::cout << "✅ Successfully deducted " << cost << " from account balance" << std::endl;
        }
        else if(amount < 0) {
            double shares = accountRepo.getPosition(accountId, symbol);
            std::cout << "Sell order - Required shares: " << std::abs(amount) 
                      << ", Available shares: " << shares << std::endl;
            if(shares < std::abs(amount)) {
                std::cerr << "ERROR: Insufficient shares for sell order" << std::endl;
                return -1;
            }
            if(!accountRepo.updatePosition(accountId, symbol, shares - std::abs(amount))) {
                std::cerr << "ERROR: Failed to update position" << std::endl;
                return -1;
            }
            std::cout << "Successfully deducted " << std::abs(amount) << " shares from account position" << std::endl;
        }
        else {
            std::cerr << "ERROR: Order amount cannot be zero" << std::endl;
            return -1;
        }
        
        int orderId = orderRepo.createOrder(accountId, symbol, amount, limitPrice);
        if(orderId > 0) {
            std::cout << "Successfully created order with ID: " << orderId << std::endl;
        }
        else {
            std::cerr << "ERROR: Failed to create order in database" << std::endl;
        }
        return orderId;
        
    } catch(const std::exception &e) {
        std::cerr << "ERROR: Exception in placeOrder: " << e.what() << std::endl;
        return -1;
    }
}

void TradingEngine::trade(std::shared_ptr<Order> buyOrder, std::shared_ptr<Order> sellOrder, int qty, double tradePrice) {
    std::lock_guard<std::mutex> lock(accountsMtx);
    auto buyerIt = accounts.find(buyOrder->getAccountId());
    auto sellerIt = accounts.find(sellOrder->getAccountId());
    if(buyerIt == accounts.end() || sellerIt == accounts.end()){
        throw std::runtime_error("Account not found");
    }
    Account &buyer = buyerIt->second;
    Account &seller = sellerIt->second;
    long t = time(nullptr);
    Record rec(qty, tradePrice, t);
    buyOrder->addExecution(qty, rec);
    sellOrder->addExecution(qty, rec);
    double cost = qty * tradePrice;
    buyer.deductFrozenFunds(cost);
    seller.deductFrozenPosition(sellOrder->getSymbol(), qty);
    buyer.addPosition(buyOrder->getSymbol(), qty);
    seller.addBalance(cost);
    buyOrder->reduceOpenQty(qty);
    sellOrder->reduceOpenQty(qty);
}

bool TradingEngine::executeTransaction(const Order &buyOrder, const Order &sellOrder, 
                                         double amount, double price) {
    try {
        // Implement database-backed transaction
        return true;
    } catch(const std::exception &e) {
        std::cerr << "Error executing transaction: " << e.what() << std::endl;
        return false;
    }
}

bool TradingEngine::cancelOrder(int orderId) {
    auto order = orderRepo.getOrder(orderId);
    if(!order){
        return false;
    }
    bool canceled = orderRepo.cancelOrder(orderId);
    if(canceled) {
        // If it was a buy order, refund the money
        if(order->getAmount() > 0) {
            double refundAmount = order->getOpenAmount() * order->getLimitPrice();
            double currentBalance = accountRepo.getBalance(order->getAccountId());
            accountRepo.updateBalance(order->getAccountId(), currentBalance + refundAmount);
        }
        else {
            // If it was a sell order, return the shares
            double returnShares = std::abs(order->getOpenAmount());
            double currentShares = accountRepo.getPosition(order->getAccountId(), order->getSymbol());
            accountRepo.updatePosition(order->getAccountId(), order->getSymbol(), currentShares + returnShares);
        }
    }
    return canceled;
}

bool TradingEngine::queryOrder(int order_id, std::string &XML_info) {
    auto order = orderRepo.getOrder(order_id);
    if(!order){
        XML_info = "<status id=\"" + std::to_string(order_id) + "\">";
        XML_info += "<error>Order not found</error>";
        XML_info += "</status>";
        return false;
    }
    XML_info = "<status id=\"" + std::to_string(order_id) + "\">";
    if(order->getStatus() == OrderStatus::OPEN && order->getOpenAmount() != 0){
        XML_info += "<open shares=\"" + std::to_string(std::abs(order->getOpenAmount())) + "\"/>";
    }
    auto executions = orderRepo.getOrderExecutions(order_id);
    for(const auto &exec : executions){
        XML_info += "<executed shares=\"" + std::to_string(std::abs(exec.amount)) +
                    "\" price=\"" + std::to_string(exec.price) +
                    "\" time=\"" + exec.executedAt + "\"/>";
    }
    if(order->getStatus() == OrderStatus::CANCELED && order->getOpenAmount() != 0){
        XML_info += "<canceled shares=\"" + std::to_string(std::abs(order->getOpenAmount())) +
                    "\" time=\"" + std::to_string(order->getCancelTime()) + "\"/>";
    }
    XML_info += "</status>";
    return true;
}

std::string TradingEngine::processRequest(const std::string &xmlStr) {
    std::cout << "\n======== TRADING ENGINE REQUEST PROCESSING ========" << std::endl;
    std::cout << "Received XML request: " << xmlStr << std::endl;
    
    XMLDocument doc;
    XMLError err = doc.Parse(xmlStr.c_str());
    if(err != XML_SUCCESS){
        std::cerr << "ERROR: Failed to parse XML, error code: " << err << std::endl;
        return "<results><error>XML parsing failed</error></results>";
    }
    
    std::cout << "Successfully parsed XML" << std::endl;
    
    XMLDocument respDoc;
    XMLElement *results = respDoc.NewElement("results");
    respDoc.InsertFirstChild(results);
    
    XMLElement *root = doc.RootElement();
    if(!root){
        std::cerr << "ERROR: No root element found in XML" << std::endl;
        return "<results><error>Missing root element</error></results>";
    }
    
    std::string rootName = root->Name();
    std::cout << "Root element name: " << rootName << std::endl;
    
    if(rootName == "create"){
        std::cout << "Processing CREATE request..." << std::endl;
        processCreate(root, results, &respDoc);
    }
    else if(rootName == "transactions"){
        std::cout << "Processing TRANSACTIONS request..." << std::endl;
        processTransaction(root, results);
    }
    else{
        std::cerr << "ERROR: Unknown root element: " << rootName << std::endl;
        XMLElement *error = respDoc.NewElement("error");
        error->SetText("Unknown request type");
        results->InsertEndChild(error);
    }
    
    XMLPrinter printer;
    respDoc.Print(&printer);
    std::string response = printer.CStr();
    
    std::cout << "Generated response: " << response << std::endl;
    std::cout << "======== TRADING ENGINE PROCESSING COMPLETE ========\n" << std::endl;
    
    return response;
}

void TradingEngine::processCreate(const XMLElement *root, XMLElement *results, XMLDocument *respDoc) {
    std::cout << "==== PROCESSING CREATE REQUEST ====" << std::endl;
    
    for(const XMLElement *accountElem = root->FirstChildElement("account"); 
        accountElem; 
        accountElem = accountElem->NextSiblingElement("account")){
        
        const char *idStr = accountElem->Attribute("id");
        const char *balanceStr = accountElem->Attribute("balance");
        
        std::cout << "Processing account element - id: " << (idStr ? idStr : "null") 
                  << ", balance: " << (balanceStr ? balanceStr : "null") << std::endl;
        
        if(!idStr || !balanceStr){
            std::cerr << "ERROR: Missing required attributes for account" << std::endl;
            XMLElement *errorElem = respDoc->NewElement("error");
            if(idStr) errorElem->SetAttribute("id", idStr);
            errorElem->SetText("Missing account attributes");
            results->InsertEndChild(errorElem);
            continue;
        }
        
        std::string accountId = idStr;
        double balance = 0.0;
        
        try {
            balance = std::stod(balanceStr);
        } catch(const std::exception &e){
            std::cerr << "ERROR: Invalid balance format: " << e.what() << std::endl;
            XMLElement *errorElem = respDoc->NewElement("error");
            errorElem->SetAttribute("id", accountId.c_str());
            errorElem->SetText("Invalid balance format");
            results->InsertEndChild(errorElem);
            continue;
        }
        
        std::cout << "Creating account in database: " << accountId << " with balance: " << balance << std::endl;
        
        try {
            bool success = accountRepo.createAccount(accountId, balance);
            
            if(success){
                std::cout << "Successfully created account in database" << std::endl;
                XMLElement *createdElem = respDoc->NewElement("created");
                createdElem->SetAttribute("id", accountId.c_str());
                results->InsertEndChild(createdElem);
            }
            else{
                std::cerr << "ERROR: Failed to create account in database (already exists)" << std::endl;
                XMLElement *errorElem = respDoc->NewElement("error");
                errorElem->SetAttribute("id", accountId.c_str());
                errorElem->SetText("Account already exists");
                results->InsertEndChild(errorElem);
            }
        } catch(const std::exception &e){
            std::cerr << "ERROR: Database exception: " << e.what() << std::endl;
            XMLElement *errorElem = respDoc->NewElement("error");
            errorElem->SetAttribute("id", accountId.c_str());
            errorElem->SetText(std::string("Database error: ").append(e.what()).c_str());
            results->InsertEndChild(errorElem);
        }
    }
    
    for(const XMLElement *symbolElem = root->FirstChildElement("symbol"); 
        symbolElem; 
        symbolElem = symbolElem->NextSiblingElement("symbol")){
        
        const char *symStr = symbolElem->Attribute("sym");
        if(!symStr){
            std::cerr << "ERROR: Missing sym attribute for symbol" << std::endl;
            XMLElement *errorElem = respDoc->NewElement("error");
            errorElem->SetText("Missing sym attribute");
            results->InsertEndChild(errorElem);
            continue;
        }
        
        std::string symbol = symStr;
        std::cout << "Processing symbol: " << symbol << std::endl;
        
        for(const XMLElement *accountElem = symbolElem->FirstChildElement("account"); 
            accountElem; 
            accountElem = accountElem->NextSiblingElement("account")){
            
            const char *idStr = accountElem->Attribute("id");
            const char *sharesText = accountElem->GetText();
            
            std::cout << "Adding symbol to account - id: " << (idStr ? idStr : "null") 
                      << ", shares: " << (sharesText ? sharesText : "null") << std::endl;
            
            if(!idStr || !sharesText){
                std::cerr << "ERROR: Missing id or shares for symbol position" << std::endl;
                XMLElement *errorElem = respDoc->NewElement("error");
                errorElem->SetAttribute("sym", symbol.c_str());
                if(idStr) errorElem->SetAttribute("id", idStr);
                errorElem->SetText("Missing account ID or shares");
                results->InsertEndChild(errorElem);
                continue;
            }
            
            double shares = 0.0;
            try {
                shares = std::stod(sharesText);
            } catch(const std::exception &e){
                std::cerr << "ERROR: Invalid shares format: " << e.what() << std::endl;
                XMLElement *errorElem = respDoc->NewElement("error");
                errorElem->SetAttribute("sym", symbol.c_str());
                errorElem->SetAttribute("id", idStr);
                errorElem->SetText("Invalid shares format");
                results->InsertEndChild(errorElem);
                continue;
            }
            
            bool success = false;
            try {
                success = addSymbolToAccount(symbol, idStr, shares);
                
                if(success){
                    std::cout << "Successfully added " << shares << " shares of " << symbol 
                              << " to account " << idStr << std::endl;
                    XMLElement *createdElem = respDoc->NewElement("created");
                    createdElem->SetAttribute("sym", symbol.c_str());
                    createdElem->SetAttribute("id", idStr);
                    results->InsertEndChild(createdElem);
                }
                else{
                    std::cerr << "ERROR: Failed to add symbol to account" << std::endl;
                    XMLElement *errorElem = respDoc->NewElement("error");
                    errorElem->SetAttribute("sym", symbol.c_str());
                    errorElem->SetAttribute("id", idStr);
                    errorElem->SetText("Failed to add symbol to account");
                    results->InsertEndChild(errorElem);
                }
            } catch(const std::exception &e){
                std::cerr << "ERROR: Exception adding symbol to account: " << e.what() << std::endl;
                XMLElement *errorElem = respDoc->NewElement("error");
                errorElem->SetAttribute("sym", symbol.c_str());
                errorElem->SetAttribute("id", idStr);
                errorElem->SetText(std::string("Database error: ").append(e.what()).c_str());
                results->InsertEndChild(errorElem);
            }
        }
    }
    
    std::cout << "==== CREATE REQUEST PROCESSING COMPLETE ====" << std::endl;
}

XMLElement* TradingEngine::buildOrderElement(XMLDocument *doc, std::shared_ptr<Order> order) {
    XMLElement *orderElem = doc->NewElement("order");
    orderElem->SetAttribute("id", order->getOrderId());
    orderElem->SetAttribute("symbol", order->getSymbol().c_str());
    orderElem->SetAttribute("side", (order->getSide() == OrderSide::BUY ? "BUY" : "SELL"));
    orderElem->SetAttribute("price", order->getLimitPrice());
    orderElem->SetAttribute("quantity", order->getQuantity());
    std::string status;
    if(order->getOpenQuantity() == 0){
        status = (order->getCanceled() == order->getQuantity()) ? "CANCELLED" : "FILLED";
    }
    else{
        status = "PARTIALLY_FILLED";
    }
    orderElem->SetAttribute("status", status.c_str());
    const auto &records = order->getRecords();
    if(!records.empty()){
        XMLElement *execs = doc->NewElement("executions");
        for(const auto &rec : records){
            XMLElement *exec = doc->NewElement("execution");
            exec->SetAttribute("shares", rec.getShares());
            exec->SetAttribute("price", rec.getPrice());
            int64_t ts = static_cast<int64_t>(rec.getTimestamp());
            exec->SetAttribute("timestamp", ts);
            execs->InsertEndChild(exec);
        }
        orderElem->InsertEndChild(execs);
    }
    if(order->getCanceled() > 0){
        XMLElement *cancel = doc->NewElement("cancellation");
        cancel->SetAttribute("shares", order->getCanceled());
        int64_t cancelTs = static_cast<int64_t>(order->getCancelTime());
        cancel->SetAttribute("timestamp", cancelTs);
        orderElem->InsertEndChild(cancel);
    }
    return orderElem;
}

void TradingEngine::processQuery(const XMLElement *queryElem, XMLElement *results, const std::string &accountId) {
    std::cout << "==== PROCESSING QUERY ====" << std::endl;
    
    const char *idStr = queryElem->Attribute("id");
    if(!idStr){
        std::cerr << "ERROR: Missing id attribute in query" << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetText("Missing order id in query");
        results->InsertEndChild(errorElem);
        return;
    }
    
    int orderId = 0;
    try {
        orderId = std::stoi(idStr);
        std::cout << "Querying order ID: " << orderId << std::endl;
    } catch(const std::exception &e){
        std::cerr << "ERROR: Invalid order ID format: " << e.what() << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Invalid order ID format");
        results->InsertEndChild(errorElem);
        return;
    }
    
    auto order = orderRepo.getOrder(orderId);
    if(!order){
        std::cerr << "ERROR: Order not found: " << orderId << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Order not found");
        results->InsertEndChild(errorElem);
        return;
    }
    
    if(order->getAccountId() != accountId){
        std::cerr << "ERROR: Order " << orderId << " does not belong to account " << accountId << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Order does not belong to this account");
        results->InsertEndChild(errorElem);
        return;
    }
    
    XMLElement *statusElem = results->GetDocument()->NewElement("status");
    statusElem->SetAttribute("id", idStr);
    
    if(order->getStatus() == OrderStatus::OPEN && order->getOpenAmount() != 0){
        XMLElement *openElem = results->GetDocument()->NewElement("open");
        openElem->SetAttribute("shares", std::to_string(std::abs(order->getOpenAmount())).c_str());
        statusElem->InsertEndChild(openElem);
    }
    
    auto executions = orderRepo.getOrderExecutions(orderId);
    for(const auto &exec : executions){
        XMLElement *executedElem = results->GetDocument()->NewElement("executed");
        executedElem->SetAttribute("shares", std::to_string(std::abs(exec.amount)).c_str());
        executedElem->SetAttribute("price", std::to_string(exec.price).c_str());
        executedElem->SetAttribute("time", exec.executedAt.c_str());
        statusElem->InsertEndChild(executedElem);
    }
    
    if(order->getStatus() == OrderStatus::CANCELED && order->getOpenAmount() != 0){
        XMLElement *canceledElem = results->GetDocument()->NewElement("canceled");
        canceledElem->SetAttribute("shares", std::to_string(std::abs(order->getOpenAmount())).c_str());
        canceledElem->SetAttribute("time", std::to_string(order->getCancelTime()).c_str());
        statusElem->InsertEndChild(canceledElem);
    }
    
    results->InsertEndChild(statusElem);
    std::cout << "==== QUERY PROCESSING COMPLETE ====" << std::endl;
}

void TradingEngine::processCancel(const XMLElement *cancelElem, XMLElement *results, const std::string &accountId) {
    std::cout << "==== PROCESSING CANCEL ====" << std::endl;
    
    const char *idStr = cancelElem->Attribute("id");
    if(!idStr){
        std::cerr << "ERROR: Missing id attribute in cancel" << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetText("Missing order id in cancel request");
        results->InsertEndChild(errorElem);
        return;
    }
    
    int orderId = 0;
    try {
        orderId = std::stoi(idStr);
        std::cout << "Canceling order ID: " << orderId << std::endl;
    } catch(const std::exception &e){
        std::cerr << "ERROR: Invalid order ID format: " << e.what() << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Invalid order ID format");
        results->InsertEndChild(errorElem);
        return;
    }
    
    auto order = orderRepo.getOrder(orderId);
    if(!order){
        std::cerr << "ERROR: Order not found: " << orderId << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Order not found");
        results->InsertEndChild(errorElem);
        return;
    }
    
    if(order->getAccountId() != accountId){
        std::cerr << "ERROR: Order " << orderId << " does not belong to account " << accountId << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Order does not belong to this account");
        results->InsertEndChild(errorElem);
        return;
    }
    
    bool success = cancelOrder(orderId);
    
    if(success){
        std::cout << "Successfully canceled order: " << orderId << std::endl;
        auto canceledOrder = orderRepo.getOrder(orderId);
        
        XMLElement *canceledElem = results->GetDocument()->NewElement("canceled");
        canceledElem->SetAttribute("id", idStr);
        results->InsertEndChild(canceledElem);
    }
    else{
        std::cerr << "ERROR: Failed to cancel order: " << orderId << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Failed to cancel order");
        results->InsertEndChild(errorElem);
    }
    
    std::cout << "==== CANCEL PROCESSING COMPLETE ====" << std::endl;
}

void TradingEngine::processTransaction(const XMLElement *root, XMLElement *results) {
    std::cout << "==== PROCESSING TRANSACTION REQUEST ====" << std::endl;
    
    const char *accountIdStr = root->Attribute("id");
    if(!accountIdStr){
        std::cerr << "ERROR: Missing account ID in transaction request" << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetText("Missing account ID");
        results->InsertEndChild(errorElem);
        return;
    }
    
    std::string accountId = accountIdStr;
    std::cout << "Transaction for account: " << accountId << std::endl;
    
    std::cout << "Checking if account exists in database..." << std::endl;
    bool accountExists = false;
    
    try {
        accountExists = accountRepo.accountExists(accountId);
        
        if(accountExists){
            std::cout << "Account exists in database" << std::endl;
        }
        else{
            std::cerr << "ERROR: Account not found in database" << std::endl;
        }
    } catch(const std::exception &e){
        std::cerr << "ERROR: Database exception: " << e.what() << std::endl;
        accountExists = false;
    }
    
    if(!accountExists){
        std::cerr << "Account does not exist, returning error" << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", accountId.c_str());
        errorElem->SetText("Account not found");
        results->InsertEndChild(errorElem);
        return;
    }
    
    for(const XMLElement *childElem = root->FirstChildElement(); 
        childElem; 
        childElem = childElem->NextSiblingElement()){
        
        std::string elemType = childElem->Name();
        std::cout << "Processing child element: " << elemType << std::endl;
        
        if(elemType == "order"){
            processOrder(childElem, results, accountId);
        }
        else if(elemType == "query"){
            processQuery(childElem, results, accountId);
        }
        else if(elemType == "cancel"){
            processCancel(childElem, results, accountId);
        }
    }
    
    std::cout << "==== TRANSACTION REQUEST PROCESSING COMPLETE ====" << std::endl;
}

void TradingEngine::processOrder(const XMLElement *orderElem, XMLElement *results, const std::string &accountId) {
    std::cout << "==== PROCESSING ORDER ====" << std::endl;
    
    const char *symStr = orderElem->Attribute("sym");
    const char *amountStr = orderElem->Attribute("amount");
    const char *limitStr = orderElem->Attribute("limit");
    
    std::cout << "Order details - account: " << accountId
              << ", symbol: " << (symStr ? symStr : "null")
              << ", amount: " << (amountStr ? amountStr : "null")
              << ", limit: " << (limitStr ? limitStr : "null") << std::endl;
    
    if(!symStr || !amountStr || !limitStr){
        std::cerr << "ERROR: Missing required attributes for order" << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        if(symStr) errorElem->SetAttribute("sym", symStr);
        errorElem->SetText("Missing required order attributes");
        results->InsertEndChild(errorElem);
        return;
    }
    
    double amount = 0.0;
    double limitPrice = 0.0;
    
    try {
        amount = std::stod(amountStr);
        limitPrice = std::stod(limitStr);
        
        std::cout << "Parsed values - amount: " << amount << ", limit: " << limitPrice << std::endl;
    } catch(const std::exception &e){
        std::cerr << "ERROR: Invalid numeric format: " << e.what() << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("sym", symStr);
        errorElem->SetText("Invalid numeric format in order");
        results->InsertEndChild(errorElem);
        return;
    }
    
    std::cout << "Debug: Checking account balance and position before order" << std::endl;
    try {
        double balance = accountRepo.getBalance(accountId);
        double position = accountRepo.getPosition(accountId, symStr);
        std::cout << "Account " << accountId << " has balance: " << balance 
                  << " and position in " << symStr << ": " << position << std::endl;
    } catch(const std::exception &e){
        std::cout << "DEBUG: Error checking balance/position: " << e.what() << std::endl;
    }
    
    try {
        int orderId = placeOrder(accountId, symStr, amount, limitPrice);
        
        if(orderId > 0){
            std::cout << "Successfully placed order with ID: " << orderId << std::endl;
            
            XMLElement *openedElem = results->GetDocument()->NewElement("opened");
            openedElem->SetAttribute("id", std::to_string(orderId).c_str());
            openedElem->SetAttribute("sym", symStr);
            openedElem->SetAttribute("amount", amountStr);
            openedElem->SetAttribute("limit", limitStr);
            results->InsertEndChild(openedElem);
        }
        else{
            std::string errorMsg;
            
            if(amount > 0){
                double cost = amount * limitPrice;
                double balance = accountRepo.getBalance(accountId);
                if(balance < cost){
                    errorMsg = "Insufficient balance for buy order";
                }
            }
            else{
                double shares = accountRepo.getPosition(accountId, symStr);
                if(shares < std::abs(amount)){
                    errorMsg = "Insufficient shares for sell order";
                }
            }
            
            if(errorMsg.empty()){
                errorMsg = "Order creation failed";
            }
            
            std::cerr << "ERROR: " << errorMsg << std::endl;
            XMLElement *errorElem = results->GetDocument()->NewElement("error");
            errorElem->SetAttribute("sym", symStr);
            errorElem->SetText(errorMsg.c_str());
            results->InsertEndChild(errorElem);
        }
    } catch(const std::exception &e){
        std::cerr << "ERROR: Exception placing order: " << e.what() << std::endl;
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("sym", symStr);
        errorElem->SetText(std::string("Order error: ").append(e.what()).c_str());
        results->InsertEndChild(errorElem);
    }
    
    std::cout << "==== ORDER PROCESSING COMPLETE ====" << std::endl;
}

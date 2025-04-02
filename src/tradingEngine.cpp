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
    std::cout << "\n========== TRADING ENGINE REQUEST ==========\n";
    std::cout << "Received XML (" << xmlStr.size() << " bytes): " << xmlStr << std::endl;

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
    std::string response = printer.CStr();

    std::cout << "Generated response: " << response << std::endl;
    std::cout << "========== END TRADING ENGINE REQUEST ==========\n";

    return response;
}

void TradingEngine::processCreate(const tinyxml2::XMLElement *root, tinyxml2::XMLElement *results, tinyxml2::XMLDocument *respDoc) {
    std::cout << "DEBUG: Inside processCreate" << std::endl;
    
    // Debug: Count elements
    int accountCount = 0;
    int symbolCount = 0;
    
    // Process account creation
    for (const tinyxml2::XMLElement *accountElem = root->FirstChildElement("account"); 
         accountElem; 
         accountElem = accountElem->NextSiblingElement("account")) {
        
        accountCount++;
        const char* idStr = accountElem->Attribute("id");
        const char* balanceStr = accountElem->Attribute("balance");
        
        std::cout << "DEBUG: Processing account element #" << accountCount 
                  << ", id=" << (idStr ? idStr : "null") 
                  << ", balance=" << (balanceStr ? balanceStr : "null") << std::endl;
        
        if (!idStr || !balanceStr) {
            std::cout << "DEBUG: Missing attributes in account element" << std::endl;
            tinyxml2::XMLElement *errorElem = respDoc->NewElement("error");
            errorElem->SetAttribute("id", idStr ? idStr : "");
            errorElem->SetText("Missing attribute(s)");
            results->InsertEndChild(errorElem);
            continue;
        }
        
        std::string accountId = idStr;
        double balance = 0.0;
        try {
            balance = std::stod(balanceStr);
        } catch (const std::exception& e) {
            std::cout << "DEBUG: Invalid balance format: " << e.what() << std::endl;
            tinyxml2::XMLElement *errorElem = respDoc->NewElement("error");
            errorElem->SetAttribute("id", accountId.c_str());
            errorElem->SetText("Invalid balance format");
            results->InsertEndChild(errorElem);
            continue;
        }
        
        // Try to create the account
        std::string msg;
        bool success = createAccount(accountId, balance, msg);
        
        std::cout << "DEBUG: Account creation result: " << (success ? "success" : "failure") 
                  << ", message: " << msg << std::endl;
        
        if (success) {
            // Account created successfully
            tinyxml2::XMLElement *createdElem = respDoc->NewElement("created");
            createdElem->SetAttribute("id", accountId.c_str());
            results->InsertEndChild(createdElem);
        } else {
            // Account already exists or other error
            tinyxml2::XMLElement *errorElem = respDoc->NewElement("error");
            errorElem->SetAttribute("id", accountId.c_str());
            errorElem->SetText(msg.c_str());
            results->InsertEndChild(errorElem);
        }
    }
    
    // Process symbol creation
    for (const tinyxml2::XMLElement *symbolElem = root->FirstChildElement("symbol"); 
         symbolElem; 
         symbolElem = symbolElem->NextSiblingElement("symbol")) {
        
        symbolCount++;
        const char* symStr = symbolElem->Attribute("sym");
        
        std::cout << "DEBUG: Processing symbol element #" << symbolCount 
                  << ", sym=" << (symStr ? symStr : "null") << std::endl;
        
        if (!symStr) {
            std::cout << "DEBUG: Missing sym attribute in symbol element" << std::endl;
            continue; // Skip this element if no symbol attribute
        }
        
        std::string symbol = symStr;
        
        // Process accounts within symbol
        int accountPositionCount = 0;
        for (const tinyxml2::XMLElement *accountElem = symbolElem->FirstChildElement("account"); 
             accountElem; 
             accountElem = accountElem->NextSiblingElement("account")) {
            
            accountPositionCount++;
            const char* idStr = accountElem->Attribute("id");
            
            std::cout << "DEBUG: Processing symbol->account element #" << accountPositionCount 
                      << ", id=" << (idStr ? idStr : "null") << std::endl;
            
            if (!idStr) {
                std::cout << "DEBUG: Missing id attribute in symbol->account element" << std::endl;
                continue; // Skip this element if no id attribute
            }
            
            std::string accountId = idStr;
            double shares = 0;
            
            // Get the number of shares from the element text
            const char* sharesText = accountElem->GetText();
            if (sharesText) {
                try {
                    shares = std::stod(sharesText);
                    std::cout << "DEBUG: Shares value: " << shares << std::endl;
                } catch (const std::exception& e) {
                    std::cout << "DEBUG: Invalid shares format: " << e.what() << std::endl;
                    tinyxml2::XMLElement *errorElem = respDoc->NewElement("error");
                    errorElem->SetAttribute("sym", symbol.c_str());
                    errorElem->SetAttribute("id", accountId.c_str());
                    errorElem->SetText("Invalid shares format");
                    results->InsertEndChild(errorElem);
                    continue;
                }
            } else {
                std::cout << "DEBUG: No shares text provided" << std::endl;
            }
            
            // Try to add shares to the account
            std::string msg;
            bool success = createSymbol(accountId, symbol, shares, msg);
            
            std::cout << "DEBUG: Symbol creation result: " << (success ? "success" : "failure") 
                      << ", message: " << msg << std::endl;
            
            if (success) {
                // Shares added successfully
                tinyxml2::XMLElement *createdElem = respDoc->NewElement("created");
                createdElem->SetAttribute("sym", symbol.c_str());
                createdElem->SetAttribute("id", accountId.c_str());
                results->InsertEndChild(createdElem);
            } else {
                // Error adding shares
                tinyxml2::XMLElement *errorElem = respDoc->NewElement("error");
                errorElem->SetAttribute("sym", symbol.c_str());
                errorElem->SetAttribute("id", accountId.c_str());
                errorElem->SetText(msg.c_str());
                results->InsertEndChild(errorElem);
            }
        }
        
        std::cout << "DEBUG: Found " << accountPositionCount << " accounts in symbol " << symbol << std::endl;
    }
    
    std::cout << "DEBUG: Processed " << accountCount << " accounts and " << symbolCount << " symbols" << std::endl;
}

tinyxml2::XMLElement* TradingEngine::buildOrderElement(tinyxml2::XMLDocument *doc, std::shared_ptr<Order> order) {
    // Implement as needed
    return nullptr;
}

void TradingEngine::processTransaction(const tinyxml2::XMLElement *root, tinyxml2::XMLElement *results) {
    std::cout << "DEBUG: Inside processTransaction" << std::endl;
    
    // Get the account ID attribute
    const char* accountIdStr = root->Attribute("id");
    if (!accountIdStr) {
        std::cout << "DEBUG: Missing account ID in transactions tag" << std::endl;
        tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetText("Missing account ID");
        results->InsertEndChild(errorElem);
        return;
    }
    
    std::string accountId = accountIdStr;
    std::cout << "DEBUG: Processing transactions for account: " << accountId << std::endl;
    
    // Check if account exists
    bool accountExists = accountRepo.accountExists(accountId);
    if (!accountExists) {
        std::cout << "DEBUG: Account does not exist: " << accountId << std::endl;
        
        // Create error responses for all child elements
        for (const tinyxml2::XMLElement *child = root->FirstChildElement(); 
             child; 
             child = child->NextSiblingElement()) {
            
            std::string elemName = child->Name();
            std::cout << "DEBUG: Processing child element: " << elemName << std::endl;
            
            if (elemName == "order") {
                tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
                errorElem->SetAttribute("sym", child->Attribute("sym") ? child->Attribute("sym") : "");
                errorElem->SetAttribute("amount", child->Attribute("amount") ? child->Attribute("amount") : "");
                errorElem->SetAttribute("limit", child->Attribute("limit") ? child->Attribute("limit") : "");
                errorElem->SetText("Account not found");
                results->InsertEndChild(errorElem);
            } 
            else if (elemName == "query" || elemName == "cancel") {
                tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
                errorElem->SetAttribute("id", child->Attribute("id") ? child->Attribute("id") : "");
                errorElem->SetText("Account not found");
                results->InsertEndChild(errorElem);
            }
        }
        return;
    }
    
    // Process all child elements
    for (const tinyxml2::XMLElement *child = root->FirstChildElement(); 
         child; 
         child = child->NextSiblingElement()) {
        
        std::string elemName = child->Name();
        std::cout << "DEBUG: Processing child element: " << elemName << std::endl;
        
        if (elemName == "order") {
            processOrder(child, results, accountId);
        } 
        else if (elemName == "query") {
            processQuery(child, results, accountId);
        } 
        else if (elemName == "cancel") {
            processCancel(child, results, accountId);
        }
        else {
            std::cout << "DEBUG: Unknown element in transactions: " << elemName << std::endl;
            tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
            errorElem->SetText(("Unknown element: " + elemName).c_str());
            results->InsertEndChild(errorElem);
        }
    }
}

void TradingEngine::processOrder(const tinyxml2::XMLElement *orderElem, tinyxml2::XMLElement *results, const std::string &accountId) {
    std::cout << "DEBUG: Processing order element" << std::endl;
    
    // Get required attributes
    const char* symStr = orderElem->Attribute("sym");
    const char* amountStr = orderElem->Attribute("amount");
    const char* limitStr = orderElem->Attribute("limit");
    
    if (!symStr || !amountStr || !limitStr) {
        std::cout << "DEBUG: Missing required attributes in order element" << std::endl;
        tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
        if (symStr) errorElem->SetAttribute("sym", symStr);
        if (amountStr) errorElem->SetAttribute("amount", amountStr);
        if (limitStr) errorElem->SetAttribute("limit", limitStr);
        errorElem->SetText("Missing required attributes");
        results->InsertEndChild(errorElem);
        return;
    }
    
    std::string symbol = symStr;
    double amount = 0.0;
    double limitPrice = 0.0;
    
    try {
        amount = std::stod(amountStr);
        limitPrice = std::stod(limitStr);
    } catch (const std::exception& e) {
        std::cout << "DEBUG: Invalid numeric format: " << e.what() << std::endl;
        tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("sym", symStr);
        errorElem->SetAttribute("amount", amountStr);
        errorElem->SetAttribute("limit", limitStr);
        errorElem->SetText("Invalid numeric format");
        results->InsertEndChild(errorElem);
        return;
    }
    
    // Place the order
    int orderId = placeOrder(accountId, symbol, amount, limitPrice);
    
    if (orderId > 0) {
        std::cout << "DEBUG: Order placed successfully, ID: " << orderId << std::endl;
        tinyxml2::XMLElement *openedElem = results->GetDocument()->NewElement("opened");
        openedElem->SetAttribute("sym", symStr);
        openedElem->SetAttribute("amount", amountStr);
        openedElem->SetAttribute("limit", limitStr);
        openedElem->SetAttribute("id", std::to_string(orderId).c_str());
        results->InsertEndChild(openedElem);
    } else {
        std::cout << "DEBUG: Order placement failed" << std::endl;
        tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("sym", symStr);
        errorElem->SetAttribute("amount", amountStr);
        errorElem->SetAttribute("limit", limitStr);
        errorElem->SetText("Order placement failed");
        results->InsertEndChild(errorElem);
    }
}

void TradingEngine::processQuery(const tinyxml2::XMLElement *queryElem, tinyxml2::XMLElement *results, const std::string &accountId) {
    std::cout << "DEBUG: Processing query element" << std::endl;
    
    const char* idStr = queryElem->Attribute("id");
    if (!idStr) {
        std::cout << "DEBUG: Missing id attribute in query element" << std::endl;
        tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetText("Missing order ID");
        results->InsertEndChild(errorElem);
        return;
    }
    
    int orderId = 0;
    try {
        orderId = std::stoi(idStr);
    } catch (const std::exception& e) {
        std::cout << "DEBUG: Invalid order ID format: " << e.what() << std::endl;
        tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Invalid order ID format");
        results->InsertEndChild(errorElem);
        return;
    }
    
    // Create status element
    tinyxml2::XMLElement *statusElem = results->GetDocument()->NewElement("status");
    statusElem->SetAttribute("id", idStr);
    
    // Get order status
    auto statuses = queryOrder(orderId);
    
    if (statuses.empty()) {
        std::cout << "DEBUG: Order not found: " << orderId << std::endl;
        tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Order not found");
        results->InsertEndChild(errorElem);
        return;
    }
    
    // Add status details - simplified for now
    bool hasOpen = false;
    bool hasCanceled = false;
    bool hasExecuted = false;
    
    for (const auto& status : statuses) {
        if (status == OrderStatus::OPEN && !hasOpen) {
            tinyxml2::XMLElement *openElem = results->GetDocument()->NewElement("open");
            openElem->SetAttribute("shares", "100"); // Placeholder
            statusElem->InsertEndChild(openElem);
            hasOpen = true;
        } 
        else if (status == OrderStatus::CANCELED && !hasCanceled) {
            tinyxml2::XMLElement *canceledElem = results->GetDocument()->NewElement("canceled");
            canceledElem->SetAttribute("shares", "100"); // Placeholder
            canceledElem->SetAttribute("time", std::to_string(time(nullptr)).c_str());
            statusElem->InsertEndChild(canceledElem);
            hasCanceled = true;
        }
        else if (status == OrderStatus::EXECUTED && !hasExecuted) {
            tinyxml2::XMLElement *executedElem = results->GetDocument()->NewElement("executed");
            executedElem->SetAttribute("shares", "100"); // Placeholder
            executedElem->SetAttribute("price", "100.0"); // Placeholder
            executedElem->SetAttribute("time", std::to_string(time(nullptr)).c_str());
            statusElem->InsertEndChild(executedElem);
            hasExecuted = true;
        }
    }
    
    // If no specific status was added, add a default one
    if (!hasOpen && !hasCanceled && !hasExecuted) {
        tinyxml2::XMLElement *openElem = results->GetDocument()->NewElement("open");
        openElem->SetAttribute("shares", "100"); // Placeholder
        statusElem->InsertEndChild(openElem);
    }
    
    results->InsertEndChild(statusElem);
}

void TradingEngine::processCancel(const tinyxml2::XMLElement *cancelElem, tinyxml2::XMLElement *results, const std::string &accountId) {
    std::cout << "DEBUG: Processing cancel element" << std::endl;
    
    const char* idStr = cancelElem->Attribute("id");
    if (!idStr) {
        std::cout << "DEBUG: Missing id attribute in cancel element" << std::endl;
        tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetText("Missing order ID");
        results->InsertEndChild(errorElem);
        return;
    }
    
    int orderId = 0;
    try {
        orderId = std::stoi(idStr);
    } catch (const std::exception& e) {
        std::cout << "DEBUG: Invalid order ID format: " << e.what() << std::endl;
        tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Invalid order ID format");
        results->InsertEndChild(errorElem);
        return;
    }
    
    // Cancel the order
    bool success = cancelOrder(orderId);
    
    if (success) {
        std::cout << "DEBUG: Order canceled successfully: " << orderId << std::endl;
        tinyxml2::XMLElement *canceledElem = results->GetDocument()->NewElement("canceled");
        canceledElem->SetAttribute("id", idStr);
        
        // Add details about cancellation
        tinyxml2::XMLElement *cancelDetailsElem = results->GetDocument()->NewElement("canceled");
        cancelDetailsElem->SetAttribute("shares", "100"); // Placeholder
        cancelDetailsElem->SetAttribute("time", std::to_string(time(nullptr)).c_str());
        canceledElem->InsertEndChild(cancelDetailsElem);
        
        results->InsertEndChild(canceledElem);
    } else {
        std::cout << "DEBUG: Order cancellation failed: " << orderId << std::endl;
        tinyxml2::XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Failed to cancel order");
        results->InsertEndChild(errorElem);
    }
}


#include "tradingEngine.hpp"
#include "dbConnection.hpp"
#include <pqxx/pqxx>
#include <iostream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <cassert>
#include <cstring>
#include "tinyxml2.h"

#ifdef __linux__
#include <pthread.h>
#endif

using namespace tinyxml2;

std::queue<std::string> TradingEngine::logQueue;
std::mutex TradingEngine::logQueueMtx;
std::condition_variable TradingEngine::logCV;
bool TradingEngine::stopLogging = false;
std::thread TradingEngine::logThread;

void TradingEngine::loggingThreadFunc() {
    while(true) {
        std::unique_lock<std::mutex> lk(logQueueMtx);
        logCV.wait(lk, []{ return !logQueue.empty() || stopLogging; });
        if(stopLogging && logQueue.empty()) {
            break;
        }
        while(!logQueue.empty()) {
            std::string msg = logQueue.front();
            logQueue.pop();
            lk.unlock();
            std::cerr << msg << std::endl;
            lk.lock();
        }
    }
}

void TradingEngine::asyncLog(const std::string &msg) {
    {
        std::lock_guard<std::mutex> lk(logQueueMtx);
        logQueue.push(msg);
    }
    logCV.notify_one();
}

void TradingEngine::pinToCore(int coreId) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreId, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

TradingEngine::TradingEngine() : orderCounter(1) {
    pinToCore(0);
    if(!logThread.joinable()) {
        stopLogging = false;
        logThread = std::thread(&TradingEngine::loggingThreadFunc);
    }
}

TradingEngine::~TradingEngine() {
    {
        std::lock_guard<std::mutex> lk(logQueueMtx);
        stopLogging = true;
    }
    logCV.notify_one();
    if(logThread.joinable()) {
        logThread.join();
    }
}

void TradingEngine::logError(const std::string &message){
    asyncLog("[Error] " + message);
}

bool TradingEngine::createAccount(const std::string &account_id, double balance, std::string &msg){
    std::unique_lock<std::shared_mutex> lock(accountsMtx);
    auto it = accounts.find(account_id);
    if(it != accounts.end()){
        msg = "Account already exists";
        return false;
    }
    accounts.try_emplace(account_id, account_id, balance);
    msg = "Account created";
    accountRepo.createAccount(account_id, balance);
    std::cout << "[DEBUG] Accounts currently in map: ";
    for (const auto &pair : accounts) {
        std::cout << pair.first << " ";
    }
    std::cout << std::endl;

    return true;
}

bool TradingEngine::createSymbol(const std::string &account_id, const std::string &symbol, double shares, std::string &msg){
    std::unique_lock<std::shared_mutex> lock(accountsMtx);
    auto accIt = accounts.find(account_id);
    if(accIt == accounts.end()){
        msg = "Account does not exist";
        return false;
    }
    Account &acc = accIt->second;
    acc.addPosition(symbol, shares);
    msg = "Symbol created";
    accountRepo.addPosition(account_id, symbol, shares);
    return true;
}

bool TradingEngine::addSymbolToAccount(const std::string& symbol, const std::string& accountId, double amount){
    return accountRepo.addPosition(accountId, symbol, amount);
}

OrderBook &TradingEngine::getOrCreateOrderBook(const std::string &symbol){
    std::unique_lock<std::shared_mutex> lock(orderBooksMtx);
    auto it = orderBooks.find(symbol);
    if(it == orderBooks.end()){
        orderBooks.try_emplace(symbol);
    }
    
    return orderBooks[symbol];
}

OrderBook *TradingEngine::getOrderBookIfExist(const std::string &symbol){
    std::shared_lock<std::shared_mutex> lock(orderBooksMtx);
    auto it = orderBooks.find(symbol);
    if(it != orderBooks.end()){
        return &(it->second);
    }
    return nullptr;
}

void TradingEngine::matchOrders(const std::string &symbol){
    std::cout << "[DEBUG] Enter matchOrders for symbol=" << symbol << std::endl;

    // Acquire per-symbol lock to prevent concurrent matching on the same symbol
    std::mutex* symbolLock = nullptr;
    {
        std::lock_guard<std::mutex> mapLock(symbolLocksMapMtx);
        auto it = symbolMatchingLocks.find(symbol);
        if (it == symbolMatchingLocks.end()) {
            symbolMatchingLocks[symbol] = std::make_unique<std::mutex>();
        }
        symbolLock = symbolMatchingLocks[symbol].get();
    }

    std::lock_guard<std::mutex> lock(*symbolLock);
    std::cout << "[DEBUG] Acquired lock for symbol=" << symbol << std::endl;

    // Get orders from OrderBook cache instead of DB
    std::vector<std::shared_ptr<Order>> buyOrders;
    std::vector<std::shared_ptr<Order>> sellOrders;

    {
        std::shared_lock<std::shared_mutex> lockOrderBooks(orderBooksMtx);
        auto it = orderBooks.find(symbol);
        if (it != orderBooks.end()) {
            // Copy only OPEN orders with openAmount > 0
            for (const auto& order : it->second.getBuyOrders()) {
                if (order->getStatus() == OrderStatus::OPEN && order->getOpenAmount() > 0) {
                    buyOrders.push_back(order);
                }
            }
            for (const auto& order : it->second.getSellOrders()) {
                if (order->getStatus() == OrderStatus::OPEN && order->getOpenAmount() > 0) {
                    sellOrders.push_back(order);
                }
            }
        }
    }

    std::cout << "[Cache] Loaded " << buyOrders.size() << " buy orders and "
              << sellOrders.size() << " sell orders from cache for symbol " << symbol << std::endl;

 

    while (!buyOrders.empty() && !sellOrders.empty()) {
        auto buyOrder = buyOrders.front();
        auto sellOrder = sellOrders.front();

    
        if (buyOrder->getLimitPrice() < sellOrder->getLimitPrice()) {
            std::cout << "[DEBUG] buyPrice < sellPrice => break." << std::endl;
            break;
        }

        int qty = std::min((int)std::abs(buyOrder->getOpenAmount()), (int)std::abs(sellOrder->getOpenAmount()));
        // std::cout << "[DEBUG] qty=" << qty << std::endl;
        if (qty <= 0) {
            std::cout << "[DEBUG] qty=0 => break." << std::endl;
            break;
        }

        double price = (buyOrder->getTimestamp() < sellOrder->getTimestamp()) ? buyOrder->getLimitPrice() : sellOrder->getLimitPrice();

        
        try {
            if (executeTransaction(*buyOrder, *sellOrder, qty, price)) {
                trade(buyOrder, sellOrder, qty, price);
            }
        } catch (const std::exception &e) {
            break;
        }

        if (std::abs(buyOrder->getOpenAmount()) < 1e-12) {
            buyOrders.erase(buyOrders.begin());
        }
        if (!sellOrders.empty() && std::abs(sellOrder->getOpenAmount()) < 1e-12) {
            sellOrders.erase(sellOrders.begin());
        }

    }

    // std::cout << "[DEBUG] matchOrders done." << std::endl;
}


int TradingEngine::placeOrder(const std::string &accountId, const std::string &symbol, double amount, double limitPrice){
    std::cout << "Account: " << accountId << ", Symbol: " << symbol << ", Amount: " << amount << ", Limit: " << limitPrice << std::endl;

    int orderId = -1;
    try {
        // Wrap all asset updates and order creation in a single transaction
        pqxx::work txn(DBConnection::getConnection());

        // Check if account exists
        if(!accountRepo.accountExists(txn, accountId)) {
            logError("ERROR: Account does not exist: " + accountId);
            return -1;
        }

        if(amount > 0) {
            // Buy order - deduct cash
            double cost = amount * limitPrice;
            double balance = accountRepo.getBalance(txn, accountId);
            std::cout << "Buy order - Cost: " << cost << ", Available balance: " << balance << std::endl;

            if(balance < cost) {
                logError("ERROR: Insufficient balance for buy order");
                return -1;
            }

            if(!accountRepo.updateBalance(txn, accountId, balance - cost)) {
                logError("ERROR: Failed to update balance");
                return -1;
            }
            std::cout << "Successfully deducted " << cost << " from account balance" << std::endl;
        }
        else if(amount < 0) {
            // Sell order - deduct shares
            double shares = accountRepo.getPosition(txn, accountId, symbol);
            std::cout << "Sell order - Required shares: " << std::abs(amount) << ", Available shares: " << shares << std::endl;

            if(shares < std::abs(amount)) {
                logError("ERROR: Insufficient shares for sell order");
                return -1;
            }

            if(!accountRepo.updatePosition(txn, accountId, symbol, shares - std::abs(amount))) {
                logError("ERROR: Failed to update position");
                return -1;
            }
            std::cout << "Successfully deducted " << std::abs(amount) << " shares from account position" << std::endl;
        }
        else {
            logError("ERROR: Order amount cannot be zero");
            return -1;
        }

        // Create the order within the same transaction
        orderId = orderRepo.createOrder(txn, accountId, symbol, amount, limitPrice);
        if(orderId <= 0) {
            logError("ERROR: Failed to create order in database");
            return -1;
        }

        // Commit the transaction - all changes are atomic
        txn.commit();
        std::cout << "Successfully created order with ID: " << orderId << " (transaction committed)" << std::endl;

        // After successful commit, update in-memory caches with unique_lock (write lock)
        std::shared_ptr<Order> order;
        {
            std::unique_lock<std::shared_mutex> lockOrders(ordersMtx);
            OrderSide side = amount > 0 ? OrderSide::BUY : OrderSide::SELL;
            order = std::make_shared<Order>(
                orderId,
                accountId,
                symbol,
                limitPrice,
                std::abs(amount),
                time(nullptr),
                side
            );
            order->setOpenAmount(std::abs(amount));
            order->setStatus(OrderStatus::OPEN);
            orders[orderId] = order;
            std::cout << "[Cache] Added order " << orderId << " to orders cache" << std::endl;
        }

        // Add order to OrderBook cache
        {
            std::unique_lock<std::shared_mutex> lockOrderBooks(orderBooksMtx);
            auto it = orderBooks.find(symbol);
            if (it == orderBooks.end()) {
                orderBooks.try_emplace(symbol);
            }
            orderBooks[symbol].addOrder(order);
            std::cout << "[Cache] Added order " << orderId << " to OrderBook for symbol " << symbol << std::endl;
        }

        // Match orders after successful order placement
        matchOrders(symbol);

        return orderId;

    } catch(const std::exception &e) {
        logError("ERROR: Exception in placeOrder: " + std::string(e.what()));
        return -1;
    }
}

void TradingEngine::trade(std::shared_ptr<Order> buyOrder, std::shared_ptr<Order> sellOrder, int qty, double tradePrice){
    std::string buyerId = buyOrder->getAccountId();
    std::string sellerId = sellOrder->getAccountId();
    std::string symbol = buyOrder->getSymbol();

    try {
        // Wrap all execution updates in a single transaction
        pqxx::work txn(DBConnection::getConnection());

        // Calculate cost
        double cost = qty * tradePrice;

        // Update buyer: deduct cash, add shares
        double buyerBalance = accountRepo.getBalance(txn, buyerId);
        accountRepo.updateBalance(txn, buyerId, buyerBalance - cost);

        double buyerPos = accountRepo.getPosition(txn, buyerId, symbol);
        accountRepo.updatePosition(txn, buyerId, symbol, buyerPos + qty);

        // Update seller: add cash (shares already deducted at order placement)
        double sellerBalance = accountRepo.getBalance(txn, sellerId);
        accountRepo.updateBalance(txn, sellerId, sellerBalance + cost);

        // Record execution in database
        orderRepo.recordExecution(txn, buyOrder->getOrderId(), sellOrder->getOrderId(), symbol, qty, tradePrice);

        // Calculate new open amounts WITHOUT modifying in-memory state yet
        double newBuyOpen = buyOrder->getOpenAmount() - qty;
        double newSellOpen = sellOrder->getOpenAmount() - qty;

        // Update order statuses in database
        orderRepo.updateOrderStatus(txn, buyOrder->getOrderId(),
                newBuyOpen == 0 ? OrderStatus::EXECUTED : OrderStatus::OPEN,
                newBuyOpen);
        orderRepo.updateOrderStatus(txn, sellOrder->getOrderId(),
                newSellOpen == 0 ? OrderStatus::EXECUTED : OrderStatus::OPEN,
                newSellOpen);

        // Commit the transaction - all changes are atomic
        txn.commit();

        // ONLY after successful commit, update in-memory state
        buyOrder->reduceOpenQty(qty);
        sellOrder->reduceOpenQty(qty);

        long t = time(nullptr);
        Record rec(qty, tradePrice, t);
        buyOrder->addExecution(qty, rec);
        sellOrder->addExecution(qty, rec);

        // Update in-memory cache with unique_lock (write lock) to reduce contention
        {
            std::unique_lock<std::shared_mutex> lock(ordersMtx);

            // Update buy order in cache
            auto buyIt = orders.find(buyOrder->getOrderId());
            if (buyIt != orders.end()) {
                buyIt->second->setOpenAmount(newBuyOpen);
                buyIt->second->setStatus(newBuyOpen == 0 ? OrderStatus::EXECUTED : OrderStatus::OPEN);
            }

            // Update sell order in cache
            auto sellIt = orders.find(sellOrder->getOrderId());
            if (sellIt != orders.end()) {
                sellIt->second->setOpenAmount(newSellOpen);
                sellIt->second->setStatus(newSellOpen == 0 ? OrderStatus::EXECUTED : OrderStatus::OPEN);
            }

            std::cout << "[Cache] Updated orders in cache after trade" << std::endl;
        }

        // Update OrderBook cache - remove fully executed orders
        {
            std::unique_lock<std::shared_mutex> lockOrderBooks(orderBooksMtx);
            auto it = orderBooks.find(symbol);
            if (it != orderBooks.end()) {
                if (newBuyOpen == 0) {
                    it->second.findAndRemoveOrder(buyOrder->getOrderId());
                    std::cout << "[Cache] Removed fully executed buy order " << buyOrder->getOrderId() << " from OrderBook" << std::endl;
                }
                if (newSellOpen == 0) {
                    it->second.findAndRemoveOrder(sellOrder->getOrderId());
                    std::cout << "[Cache] Removed fully executed sell order " << sellOrder->getOrderId() << " from OrderBook" << std::endl;
                }
            }
        }

        std::cout << "Trade executed atomically: " << qty << " shares of " << symbol
                  << " at price " << tradePrice << std::endl;

    } catch(const std::exception &e) {
        logError("ERROR: Exception in trade: " + std::string(e.what()));
        throw;  // Re-throw to let matchOrders handle the error
    }
}



bool TradingEngine::executeTransaction(const Order &buyOrder, const Order &sellOrder, double amount, double price) {
    try {
        return true;
    } catch(const std::exception &e) {
        // std::cerr << "Error executing transaction: " << e.what() << std::endl;
        logError("Error executing transaction: " + std::string(e.what()));
        return false;
    }
}

bool TradingEngine::cancelOrder(int orderId) {
    try {
        // Wrap order cancellation and asset refund in a single transaction
        pqxx::work txn(DBConnection::getConnection());

        auto order = orderRepo.getOrder(txn, orderId);
        if(!order){
            return false;
        }

        bool canceled = orderRepo.cancelOrder(txn, orderId);
        if(canceled) {
            if(order->getAmount() > 0) {
                // Refund cash for buy order
                double refundAmount = order->getOpenAmount() * order->getLimitPrice();
                double currentBalance = accountRepo.getBalance(txn, order->getAccountId());
                accountRepo.updateBalance(txn, order->getAccountId(), currentBalance + refundAmount);
            }
            else {
                // Return shares for sell order
                double returnShares = std::abs(order->getOpenAmount());
                double currentShares = accountRepo.getPosition(txn, order->getAccountId(), order->getSymbol());
                accountRepo.updatePosition(txn, order->getAccountId(), order->getSymbol(), currentShares + returnShares);
            }

            txn.commit();
            std::cout << "Order " << orderId << " canceled and assets refunded (transaction committed)" << std::endl;

            // Update in-memory cache with unique_lock (write lock)
            {
                std::unique_lock<std::shared_mutex> lock(ordersMtx);
                auto it = orders.find(orderId);
                if (it != orders.end()) {
                    it->second->setStatus(OrderStatus::CANCELED);
                    std::cout << "[Cache] Updated order " << orderId << " to CANCELED in cache" << std::endl;
                }
            }

            // Remove from OrderBook cache
            {
                std::unique_lock<std::shared_mutex> lockOrderBooks(orderBooksMtx);
                auto it = orderBooks.find(order->getSymbol());
                if (it != orderBooks.end()) {
                    it->second.findAndRemoveOrder(orderId);
                    std::cout << "[Cache] Removed canceled order " << orderId << " from OrderBook" << std::endl;
                }
            }
        }

        return canceled;

    } catch(const std::exception &e) {
        logError("ERROR: Exception in cancelOrder: " + std::string(e.what()));
        return false;
    }
}

bool TradingEngine::queryOrder(int order_id, std::string &XML_info) {
    std::shared_ptr<Order> order;

    // First try to read from in-memory cache with shared_lock (read lock)
    // This allows multiple concurrent queries without blocking each other
    {
        std::shared_lock<std::shared_mutex> lock(ordersMtx);
        auto it = orders.find(order_id);
        if (it != orders.end()) {
            order = it->second;
            std::cout << "[Cache] Order " << order_id << " found in cache with shared_lock (concurrent reads enabled)" << std::endl;
        }
    }

    // Fallback to DB if not in cache
    if (!order) {
        order = orderRepo.getOrder(order_id);
        std::cout << "[Cache] Order " << order_id << " not in cache, fetched from DB" << std::endl;
    }

    if(!order){
        XML_info = "<status id=\"" + std::to_string(order_id) + "\"><error>Order not found</error></status>";
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
        // std::cerr << "ERROR: Failed to parse XML, error code: " << err << std::endl;
        logError("ERROR: Failed to parse XML, error code: " + std::to_string(err));
        return "<results><error>XML parsing failed</error></results>";
    }
    std::cout << "Successfully parsed XML" << std::endl;
    XMLDocument respDoc;
    XMLElement *results = respDoc.NewElement("results");
    respDoc.InsertFirstChild(results);
    XMLElement *root = doc.RootElement();
    if(!root){
        // std::cerr << "ERROR: No root element found in XML" << std::endl;
        logError("ERROR: No root element found in XML");
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
        // std::cerr << "ERROR: Unknown root element: " << rootName << std::endl;
        logError("ERROR: Unknown root element: " + rootName);
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
    for(const XMLElement *accountElem = root->FirstChildElement("account"); accountElem; accountElem = accountElem->NextSiblingElement("account")){
        const char *idStr = accountElem->Attribute("id");
        const char *balanceStr = accountElem->Attribute("balance");
        std::cout << "Processing account element - id: " << (idStr ? idStr : "null") << ", balance: " << (balanceStr ? balanceStr : "null") << std::endl;
        if(!idStr || !balanceStr){
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
            XMLElement *errorElem = respDoc->NewElement("error");
            errorElem->SetAttribute("id", accountId.c_str());
            errorElem->SetText("Invalid balance format");
            results->InsertEndChild(errorElem);
            continue;
        }
        std::cout << "Creating account in database: " << accountId << " with balance: " << balance << std::endl;
        try {
            bool success = accountRepo.createAccount(accountId, balance);
            std::string msg;
            bool successs = createAccount(accountId, balance, msg);
            if(success){
                XMLElement *createdElem = respDoc->NewElement("created");
                createdElem->SetAttribute("id", accountId.c_str());
                results->InsertEndChild(createdElem);
            }
            else{
                XMLElement *errorElem = respDoc->NewElement("error");
                errorElem->SetAttribute("id", accountId.c_str());
                errorElem->SetText("Account already exists");
                results->InsertEndChild(errorElem);
            }
        } catch(const std::exception &e){
            XMLElement *errorElem = respDoc->NewElement("error");
            errorElem->SetAttribute("id", accountId.c_str());
            errorElem->SetText(std::string("Database error: ").append(e.what()).c_str());
            results->InsertEndChild(errorElem);
        }
    }
    for(const XMLElement *symbolElem = root->FirstChildElement("symbol"); symbolElem; symbolElem = symbolElem->NextSiblingElement("symbol")){
        const char *symStr = symbolElem->Attribute("sym");
        if(!symStr){
            XMLElement *errorElem = respDoc->NewElement("error");
            errorElem->SetText("Missing sym attribute");
            results->InsertEndChild(errorElem);
            continue;
        }
        std::string symbol = symStr;
        std::cout << "Processing symbol: " << symbol << std::endl;
        for(const XMLElement *accountElem = symbolElem->FirstChildElement("account"); accountElem; accountElem = accountElem->NextSiblingElement("account")){
            const char *idStr = accountElem->Attribute("id");
            const char *sharesText = accountElem->GetText();
            std::cout << "Adding symbol to account - id: " << (idStr ? idStr : "null") << ", shares: " << (sharesText ? sharesText : "null") << std::endl;
            if(!idStr || !sharesText){
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
                    XMLElement *createdElem = respDoc->NewElement("created");
                    createdElem->SetAttribute("sym", symbol.c_str());
                    createdElem->SetAttribute("id", idStr);
                    results->InsertEndChild(createdElem);
                }
                else{
                    XMLElement *errorElem = respDoc->NewElement("error");
                    errorElem->SetAttribute("sym", symbol.c_str());
                    errorElem->SetAttribute("id", idStr);
                    errorElem->SetText("Failed to add symbol to account");
                    results->InsertEndChild(errorElem);
                }
            } catch(const std::exception &e){
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
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Invalid order ID format");
        results->InsertEndChild(errorElem);
        return;
    }
    auto ord = orderRepo.getOrder(orderId);
    if(!ord){
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Order not found");
        results->InsertEndChild(errorElem);
        return;
    }
    if(ord->getAccountId() != accountId){
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Order does not belong to this account");
        results->InsertEndChild(errorElem);
        return;
    }
    XMLElement *statusElem = results->GetDocument()->NewElement("status");
    statusElem->SetAttribute("id", idStr);
    if(ord->getStatus() == OrderStatus::OPEN && ord->getOpenAmount() != 0){
        XMLElement *openElem = results->GetDocument()->NewElement("open");
        openElem->SetAttribute("shares", std::to_string(std::abs(ord->getOpenAmount())).c_str());
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
    if(ord->getStatus() == OrderStatus::CANCELED && ord->getOpenAmount() != 0){
        XMLElement *canceledElem = results->GetDocument()->NewElement("canceled");
        canceledElem->SetAttribute("shares", std::to_string(std::abs(ord->getOpenAmount())).c_str());
        canceledElem->SetAttribute("time", std::to_string(ord->getCancelTime()).c_str());
        statusElem->InsertEndChild(canceledElem);
    }
    results->InsertEndChild(statusElem);
    std::cout << "==== QUERY PROCESSING COMPLETE ====" << std::endl;
}

void TradingEngine::processCancel(const XMLElement *cancelElem, XMLElement *results, const std::string &accountId) {
    std::cout << "==== PROCESSING CANCEL ====" << std::endl;
    const char *idStr = cancelElem->Attribute("id");
    if(!idStr){
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
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Invalid order ID format");
        results->InsertEndChild(errorElem);
        return;
    }
    auto order = orderRepo.getOrder(orderId);
    if(!order){
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Order not found");
        results->InsertEndChild(errorElem);
        return;
    }
    if(order->getAccountId() != accountId){
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("id", idStr);
        errorElem->SetText("Order does not belong to this account");
        results->InsertEndChild(errorElem);
        return;
    }
    bool success = cancelOrder(orderId);
    if(success){
        auto canceledOrder = orderRepo.getOrder(orderId);
        XMLElement *canceledElem = results->GetDocument()->NewElement("canceled");
        canceledElem->SetAttribute("id", idStr);
        results->InsertEndChild(canceledElem);
    }
    else{
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
        // std::cerr << "ERROR: Missing account ID in transaction request" << std::endl;
        logError("ERROR: Missing account ID in transaction request");
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
            // std::cerr << "ERROR: Account not found in database" << std::endl;
            logError("ERROR: Account not found in database");
        }
    } catch(const std::exception &e){
        // std::cerr << "ERROR: Database exception: " << e.what() << std::endl;
        logError("ERROR: Database exception: " + std::string(e.what()));
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
    for(const XMLElement *childElem = root->FirstChildElement(); childElem; childElem = childElem->NextSiblingElement()){
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
        // std::cerr << "ERROR: Missing required attributes for order" << std::endl;
        logError("ERROR: Missing required attributes for order");
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
        // std::cerr << "ERROR: Invalid numeric format: " << e.what() << std::endl;
        logError("ERROR: Invalid numeric format: " + std::string(e.what()));
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("sym", symStr);
        errorElem->SetText("Invalid numeric format in order");
        results->InsertEndChild(errorElem);
        return;
    }
    
    
    try {
        double balance = accountRepo.getBalance(accountId);
        double position = accountRepo.getPosition(accountId, symStr);
        std::cout << "Account " << accountId << " has balance: " << balance 
                   << " and position in " << symStr << ": " << position << std::endl;
    } catch(const std::exception &e){
        std::cout << "Error checking balance/position: " << e.what() << std::endl;
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
            
            // std::cerr << "ERROR: " << errorMsg << std::endl;
            logError("ERROR: " + errorMsg);
            XMLElement *errorElem = results->GetDocument()->NewElement("error");
            errorElem->SetAttribute("sym", symStr);
            errorElem->SetText(errorMsg.c_str());
            results->InsertEndChild(errorElem);
        }
    } catch(const std::exception &e){
        // std::cerr << "ERROR: Exception placing order: " << e.what() << std::endl;
        logError("ERROR: Exception placing order: " + std::string(e.what()));
        XMLElement *errorElem = results->GetDocument()->NewElement("error");
        errorElem->SetAttribute("sym", symStr);
        errorElem->SetText(std::string("Order error: ").append(e.what()).c_str());
        results->InsertEndChild(errorElem);
    }
    
    std::cout << "==== ORDER PROCESSING COMPLETE ====" << std::endl;
}
#include "tradingEngine.hpp"
#include <iostream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <cassert>

TradingEngine::TradingEngine() : orderCounter(1) {}

void TradingEngine::logError(const std::string &message) {
    std::cerr << "[Error] " << message << std::endl;
}

bool TradingEngine::createAccount(const std::string &account_id, double balance, std::string &msg){
    // std::lock_guard<std::mutex> lock(globalMtx);
    std::lock_guard<std::mutex> lock(accountsMtx);
    auto it = accounts.find(account_id);
    if(it != accounts.end()){
        msg = "Account already exists";
        return false;
    }
    accounts.try_emplace(account_id, account_id, balance);
    msg = "Account created";
    return true;
}

bool TradingEngine::createSymbol(const std::string &account_id, const std::string &symbol, double shares, std::string &msg){
    // std::lock_guard<std::mutex> lock(globalMtx);
    std::lock_guard<std::mutex> lock(accountsMtx);
    auto it = accounts.find(account_id);
    if(it == accounts.end()){
        msg = "Account not found for symbol creation";
        return false;
    }
    double existing = it->second.getTotalPosition(symbol);
    try{
        it->second.addPosition(symbol, shares);
    }
    catch(const std::runtime_error &e){
        msg = e.what();
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(orderBooksMtx);
        if(orderBooks.find(symbol) == orderBooks.end()){
            orderBooks.try_emplace(symbol);
        }
    }
    
    msg = (existing > 0) ? "Symbol position updated" : "Symbol position created";
    return true;
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

void TradingEngine::matchOrders(const std::string &symbol){
    OrderBook &book = getOrCreateOrderBook(symbol);
    std::unique_lock<std::mutex> bookLock(book.mtx);

    while(!book.buyOrders.empty() && !book.sellOrders.empty()){
        auto buyIt = book.buyOrders.begin();
        auto sellIt = book.sellOrders.begin();
        auto buyOrder = *buyIt;
        auto sellOrder = *sellIt;

        if(buyOrder->getLimitPrice() < sellOrder->getLimitPrice()){
            break;
        }
        int tradeQty = std::min(buyOrder->getOpenQuantity(), sellOrder->getOpenQuantity());
        if(tradeQty <= 0){
            break;
        }
        double tradePrice = (buyOrder->getTimestamp() <= sellOrder->getTimestamp()) ? buyOrder->getLimitPrice() : sellOrder->getLimitPrice();
        try{
            bookLock.unlock();
            trade(buyOrder, sellOrder, tradeQty, tradePrice);
            bookLock.lock();
        } catch(const std::exception &e){
            if(!bookLock.owns_lock()){
                bookLock.lock();
            }
            logError("Trade failed :" + std::string(e.what()));
            break;
        }
        if(buyOrder->getOpenQuantity() == 0){
            book.buyOrders.erase(buyIt);
        }
        if(sellOrder->getOpenQuantity() == 0){
            book.sellOrders.erase(sellIt);
        }
    }
}

bool TradingEngine::placeOrder(const std::string &account_id, const std::string &symbol, double limit_price, int quantity, OrderSide side, std::string &msg, int &createdOrderId)
{
    // Validate inputs
    if(symbol.empty()) {
        msg = "Symbol cannot be empty";
        return false;
    }
    
    if(quantity <= 0) {
        msg = "Quantity must be positive";
        return false;
    }
    
    if(limit_price <= 0) {
        msg = "Limit price must be positive";
        return false;
    }

    {
        std::lock_guard<std::mutex> lockAcc(accountsMtx);
        auto it = accounts.find(account_id);
        if(it == accounts.end()){
            msg = "Account not found";
            return false;
        }
        if(side == OrderSide::SELL){
            if(!it->second.freezePosition(symbol, quantity)){
                msg = "Insufficient shares to freeze";
                return false;
            }
        } else if(side == OrderSide::BUY){
            if(!it->second.freezeBalance(limit_price * quantity)){
                msg = "Insufficient funds to freeze";
                return false;
            }
        } else {
            msg = "Invalid order side";
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lockOrd(ordersMtx);
        createdOrderId = orderCounter.fetch_add(1);
        auto orderPtr = std::make_shared<Order>(createdOrderId, account_id, symbol, limit_price, quantity, time(nullptr), side);
        orders.emplace(createdOrderId, orderPtr);
        msg = "Order placed";
    }

    OrderBook &book = getOrCreateOrderBook(symbol);
    {
        std::lock_guard<std::mutex> lockBook(book.mtx);
        auto orderPtr = orders[createdOrderId]; 
        if(side == OrderSide::BUY){
            book.buyOrders.insert(orderPtr);
        } else {
            book.sellOrders.insert(orderPtr);
        }
    }

    matchOrders(symbol);

    return true;
}


void TradingEngine::trade(std::shared_ptr<Order> buyOrder, std::shared_ptr<Order> sellOrder, int qty, double tradePrice){
    // std::lock_guard<std::mutex> lock(globalMtx);
    auto buyAccountIt = accounts.find(buyOrder->getAccountId());
    auto sellAccountIt = accounts.find(sellOrder->getAccountId());
    if(buyAccountIt == accounts.end() || sellAccountIt == accounts.end()){
        throw std::runtime_error("Account not found during trade");
    }
    Account &buyer = buyAccountIt->second;
    Account &seller = sellAccountIt->second;
    long t = time(nullptr);
    Record rec(qty, tradePrice, t);
    buyOrder->addExecution(qty, rec);
    sellOrder->addExecution(qty, rec);
    double cost = qty * tradePrice;
    buyer.deductFrozenFunds(cost);
    seller.deductFrozenPosition(sellOrder->getSymbol(), qty);
    buyer.addPosition(buyOrder->getSymbol(), qty);
    seller.addBalance(cost);
}

bool TradingEngine::cancelOrder(int order_id, std::string &msg){
    std::shared_ptr<Order> order;
    {
        std::lock_guard<std::mutex> lockOrd(ordersMtx);
        auto it = orders.find(order_id);
        if(it == orders.end()){
            msg = "Order not found";
            return false;
        }
        order = it->second;
    }

    int openQty = order->getOpenQuantity();
    if(openQty <= 0){
        msg = "Order alreay filled or cancelled";
        return false;
    }
    order->addCancel(openQty);
    order->setCancelTime(time(nullptr));
    {
        std::lock_guard<std::mutex> lockAcc(accountsMtx);
        auto acctIt = accounts.find(order->getAccountId());
        if(acctIt != accounts.end()){
            if(order->getSide() == OrderSide::BUY){
                double refund = openQty * order->getLimitPrice();
                acctIt->second.unfreezeBalance(refund);
            } else {
                acctIt->second.unfreezePosition(order->getSymbol(), openQty);
            }
        }
    }
    
    OrderBook *book = getOrderBookIfExist(order->getSymbol());
        if(!book){
            msg = "OrderBook not found";
            return false;
        }

    {
        std::lock_guard<std::mutex> bookLock(book->mtx);
        for(auto it = book->buyOrders.begin(); it != book->buyOrders.end(); ++it){
            if((*it)->getOrderId() == order_id){
                book->buyOrders.erase(it);
                msg = "Order cancelled";
                return true;
            }
        }
        for(auto it = book->sellOrders.begin(); it != book->sellOrders.end(); ++it){
            if((*it)->getOrderId() == order_id){
                book->sellOrders.erase(it);
                msg = "Order cancelled";
                return true;
            }
        }
        if(book->buyOrders.empty() && book->sellOrders.empty()){
            std::lock_guard<std::mutex> lock(orderBooksMtx);
            orderBooks.erase(order->getSymbol());
        }
    }
    msg = "Order cancelled";
    return true;
}

bool TradingEngine::queryOrder(int order_id, std::string &XML_info){
    std::lock_guard<std::mutex> lockOrd(ordersMtx);
    auto it = orders.find(order_id);
    if(it == orders.end()){
        XML_info = "Order not found";
        return false;
    }
    auto order = it->second;
    std::ostringstream oss;
    oss << "<order id=\"" << order->getOrderId() << "\">\n";
    oss << "  <symbol>" << order->getSymbol() << "</symbol>\n";
    oss << "  <price>" << order->getLimitPrice() << "</price>\n";
    oss << "  <side>" << (order->getSide() == OrderSide::BUY ? "BUY" : "SELL") << "</side>\n";
    oss << "  <quantity>" << order->getQuantity() << "</quantity>\n";
    oss << "  <filled>" << order->getFilled() << "</filled>\n";
    oss << "  <canceled>" << order->getCanceled() << "</canceled>\n";
    std::string status;
    if(order->getOpenQuantity() == 0){
        status = (order->getCanceled() == order->getQuantity()) ? "CANCELLED" : "FILLED";
    }
    else{
        status = "PARTIALLY_FILLED";
    }
    oss << "  <summary>\n";
    oss << "    <remaining>" << order->getOpenQuantity() << "</remaining>\n";
    oss << "    <status>" << status << "</status>\n";
    oss << "  </summary>\n";
    auto &rec = order->getRecords();
    if(!rec.empty()){
        oss << "  <executions>\n";
        for(const auto &r : rec){
            oss << "    <execution>\n";
            oss << "      <shares>" << r.getShares() << "</shares>\n";
            oss << "      <price>" << r.getPrice() << "</price>\n";
            oss << "      <timestamp>" << r.getTimestamp() << "</timestamp>\n";
            oss << "    </execution>\n";
        }
        oss << "  </executions>\n";
    }
    if(order->getCanceled() > 0){
        oss << "  <cancellation shares=\"" << order->getCanceled() << "\" timestamp=\"" << order->getCancelTime() << "\" />\n";
    }
    oss << "</order>";
    XML_info = oss.str();
    return true;
}

using namespace tinyxml2;


std::string TradingEngine::processRequest(const std::string &xmlStr){
    XMLDocument respDoc;
    XMLElement *rootResp = respDoc.NewElement("results");
    respDoc.InsertFirstChild(rootResp);

    XMLDocument doc;
    XMLError err = doc.Parse(xmlStr.c_str());
    if (err != XML_SUCCESS) {
        XMLElement *e = respDoc.NewElement("error");

        e->SetText(doc.ErrorStr()); 

        rootResp->InsertEndChild(e);

        XMLPrinter printer;
        respDoc.Print(&printer);
        return printer.CStr();
    }

    XMLElement *root = doc.RootElement();
    if (!root) {
        XMLElement *er = respDoc.NewElement("error");
        er->SetText("No root element");
        rootResp->InsertEndChild(er);

        XMLPrinter printer;
        respDoc.Print(&printer);
        return printer.CStr();
    }

    const char* rootName = root->Name();
    if (!rootName) {
        XMLElement *er = respDoc.NewElement("error");
        er->SetText("Root element has no name");
        rootResp->InsertEndChild(er);

        XMLPrinter printer;
        respDoc.Print(&printer);
        return printer.CStr();
    }

    if (strcmp(rootName, "create") == 0) {
        processCreate(root, rootResp, &respDoc);
    }
    else if (strcmp(rootName, "transactions") == 0) {
        processTransaction(root, rootResp);
    }
    else {
        XMLElement *er = respDoc.NewElement("error");
        er->SetText("Unknown root tag");
        rootResp->InsertEndChild(er);
    }

    XMLPrinter printer;
    respDoc.Print(&printer);
    return printer.CStr();
}

void TradingEngine::processCreate(const tinyxml2::XMLElement *root, tinyxml2::XMLElement *results, tinyxml2::XMLDocument *respDoc){
    assert(root != nullptr);
    assert(results != nullptr);
    assert(respDoc != nullptr);
    for(const XMLElement *child = root->FirstChildElement(); child; child = child->NextSiblingElement()){
        const char* childName = child->Name();
        if (!childName) {
            std::cerr << "child->Name() is NULL" << std::endl;
            continue;
        }
        if(strcmp(childName, "account") == 0){
            const char *id = child->Attribute("id");
            if (!id) {
                XMLElement *e = respDoc->NewElement("error");
                e->SetText("Missing id attribute for account");
                results->InsertEndChild(e);
                continue;
            }
            const char *balChar = child->Attribute("balance");
            double balVal = balChar ? atof(balChar) : 0.0;
            std::string msg;
            bool account = createAccount(id, balVal, msg);
            if(account){
                XMLElement *c = respDoc->NewElement("created");
                c->SetAttribute("id", id);
                results->InsertEndChild(c);
            }
            else{
                XMLElement *e = respDoc->NewElement("error");
                e->SetAttribute("id", id);
                e->SetText(msg.c_str());
                results->InsertEndChild(e);
            }
        }
        else if(strcmp(childName, "symbol") == 0){
            const char *sym = child->Attribute("sym");
            if(!sym){
                continue;
            }
            for(const XMLElement *acc = child->FirstChildElement("account"); acc; acc = acc->NextSiblingElement("account")){
                const char *id = acc->Attribute("id");
                if(!id){
                    continue;
                }
                const char *sh = acc->GetText();
                double shares = sh ? atof(sh) : 0.0;
                std::string msg;
                bool symbol = createSymbol(id, sym, shares, msg);
                if(symbol){
                    XMLElement *c = respDoc->NewElement("created");
                    c->SetAttribute("sym", sym);
                    c->SetAttribute("id", id);
                    results->InsertEndChild(c);
                }
                else{
                    XMLElement *e = respDoc->NewElement("error");
                    e->SetAttribute("sym", sym);
                    e->SetAttribute("id", id);
                    e->SetText(msg.c_str());
                    results->InsertEndChild(e);
                }
            }
        }
    }
}

XMLElement* TradingEngine::buildOrderElement(XMLDocument *doc, std::shared_ptr<Order> order){
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


void TradingEngine::processTransaction(const tinyxml2::XMLElement *root, tinyxml2::XMLElement *results){


    XMLDocument *doc = results->GetDocument();
    const char *accountId = root->Attribute("id");
    if(!accountId){
        XMLElement *er = doc->NewElement("error");
        er->SetText("Missing account id in <transactions>");
        results->InsertEndChild(er);
        return;
    }


    for(const XMLElement *child = root->FirstChildElement(); child; child = child->NextSiblingElement()) {
        const char* childName = child->Name();
        if(!childName){
            std::cerr << "child->Name() is NULL, skip" << std::endl;
            continue;
        }


        if(strcmp(childName, "order") == 0){
            const char *sym = child->Attribute("sym");
            const char *amt = child->Attribute("amount");
            const char *lim = child->Attribute("limit");




            if(!amt){
                std::cerr << "No amount attribute, skip" << std::endl;
                continue;
            }

            int amount = atoi(amt);
            double limit = lim ? atof(lim) : 0.0;

            OrderSide side = (amount >= 0 ? OrderSide::BUY : OrderSide::SELL);

            int quantity = std::abs(amount);
            std::string msg; 
            int newOrderId;
            try {
                bool orderPlaced = placeOrder(accountId, (sym ? sym : ""), limit, quantity, side, msg, newOrderId);

                if(orderPlaced) {
                    XMLElement *op = doc->NewElement("opened");
                    op->SetAttribute("sym", (sym ? sym : ""));
                    op->SetAttribute("amount", amount);
                    op->SetAttribute("limit", limit);
                    op->SetAttribute("id", newOrderId);
                    results->InsertEndChild(op);


                }
                else{
                    XMLElement *e = doc->NewElement("error");
                    e->SetAttribute("sym", (sym ? sym : ""));
                    e->SetText(msg.c_str());
                    results->InsertEndChild(e);
                }
            } catch (const std::exception &ex) {
                XMLElement *e = doc->NewElement("error");
                e->SetAttribute("sym", (sym ? sym : ""));
                e->SetText(ex.what());
                results->InsertEndChild(e);
            }
        }

        else if(strcmp(childName, "cancel") == 0){
            const char* aid = child->Attribute("id");

            if(!aid){
                XMLElement *er = doc->NewElement("error");
                er->SetText("Cancel missing id attribute");
                results->InsertEndChild(er);
                continue;
            }
            int cancelOrderId = atoi(aid);
            std::string msg;
            bool orderCancelled = cancelOrder(cancelOrderId, msg);

            if(orderCancelled){
                auto it = orders.find(cancelOrderId);
                XMLElement *ca = doc->NewElement("canceled");
                ca->SetAttribute("id", cancelOrderId);
                if(it != orders.end()){
                    XMLElement *ordElement = buildOrderElement(doc, it->second);
                    ca->InsertEndChild(ordElement);
                }
                results->InsertEndChild(ca);
               
            }
            else{
                XMLElement *er = doc->NewElement("error");
                er->SetAttribute("id", cancelOrderId);
                er->SetText(msg.c_str());
                results->InsertEndChild(er);
               
            }
        }

        else if(strcmp(childName, "query") == 0){
            const char *oid = child->Attribute("id");


            if(!oid){
                XMLElement *er = doc->NewElement("error");
                er->SetText("Query missing id attribute");
                results->InsertEndChild(er);
                continue;
            }
            int orderId = atoi(oid);
            std::string info;
            bool querySuccess = queryOrder(orderId, info);
            if(querySuccess){
                auto it = orders.find(orderId);
                if(it != orders.end()){
                    XMLElement *sta = doc->NewElement("status");
                    sta->SetAttribute("id", orderId);
                    XMLElement *ordElement = buildOrderElement(doc, it->second);
                    sta->InsertEndChild(ordElement);
                    results->InsertEndChild(sta);

                }
                else{
                    XMLElement *er = doc->NewElement("error");
                    er->SetAttribute("id", orderId);
                    er->SetText("Order not found");
                    results->InsertEndChild(er);

                }
            }
            else{
                XMLElement *er = doc->NewElement("error");
                er->SetAttribute("id", orderId);
                er->SetText(info.c_str());
                results->InsertEndChild(er);

            }
        }
        else {
            std::cerr << "unknown transaction element: " << childName << std::endl;
        }
    }
   
}


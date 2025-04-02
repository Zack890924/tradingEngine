//src/server.hpp

#ifndef SERVER_HPP
#define SERVER_HPP

#include <netinet/in.h>
#include "tradingEngine.hpp"
#include <arpa/inet.h>
#include "threadPool.hpp"



class Server {
    private:
        int port;
        int serverFd;
        TradingEngine &tradingEngine;
        void closeServer(int fd);
        static void signalHandler(int signum);
        ThreadPool threadPool;
    public:
        explicit Server(int port, TradingEngine &tradingEngine, size_t numThreads = 4);
        ~Server();

        void init();
        void setUpSignalHandler();
        void run();
        int acceptconnection(sockaddr_in &clientAddr, socklen_t &clientAddrSize);
        void handleClient(int clientFd, TradingEngine &tradingEngine);


};

#endif // SERVER_HPP
#include "server.hpp"
#include "tradingEngine.hpp"
#include <iostream>

int main(int argc, char *argv[]){
    int port = 12345;  
    int numThreads = 4;

    if (argc > 1) {
        numThreads = std::stoi(argv[1]); 
    }

    TradingEngine tradingEngine;
    Server server(port, tradingEngine, numThreads);
    try {
        server.init();
        std::cout << "Server is listening on port " << port << std::endl;
        server.run();
    } catch(const std::exception &ex) {
        std::cerr << "Server error: " << ex.what() << std::endl;
    }
    return 0;
}

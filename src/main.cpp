#include "server.hpp"
#include "dbConnection.hpp"
#include "tradingEngine.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
//main.cpp finalize
int main() {
    try {
        std::cout << "-------------------- STARTING SERVER --------------------" << std::endl;
        // Setup database connection from environment variables
        std::string host = std::getenv("DB_HOST") ? std::getenv("DB_HOST") : "localhost";
        std::string port = std::getenv("DB_PORT") ? std::getenv("DB_PORT") : "5432";
        std::string user = std::getenv("DB_USER") ? std::getenv("DB_USER") : "exchange";
        std::string password = std::getenv("DB_PASSWORD") ? std::getenv("DB_PASSWORD") : "exchange_password";
        std::string dbname = std::getenv("DB_NAME") ? std::getenv("DB_NAME") : "exchange_db";
        
        std::cout << "Database parameters:" << std::endl;
        std::cout << "- Host: " << host << std::endl;
        std::cout << "- Port: " << port << std::endl;
        std::cout << "- User: " << user << std::endl;
        std::cout << "- Database: " << dbname << std::endl;

        std::string connString = 
            "host=" + host + 
            " port=" + port + 
            " user=" + user + 
            " password=" + password + 
            " dbname=" + dbname;

        // Initialize database connection with retries
        int maxRetries = 5;
        int retryCount = 0;
        bool connected = false;

        while (!connected && retryCount < maxRetries) {
            try {
                std::cout << "Attempting to connect to database (attempt " << retryCount + 1 << "/" << maxRetries << ")" << std::endl;
                DBConnection::initialize(connString);
                connected = true;
                std::cout << "Successfully connected to database" << std::endl;
            }
            catch (const std::exception& e) {
                std::cerr << "Database connection failed: " << e.what() << std::endl;
                retryCount++;
                
                if (retryCount < maxRetries) {
                    std::cout << "Retrying in 5 seconds..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            }
        }

        if (!connected) {
            throw std::runtime_error("Failed to connect to database after " + std::to_string(maxRetries) + " attempts");
        }
        
        std::cout << "Creating trading engine..." << std::endl;
        TradingEngine tradingEngine;
        std::cout << "Trading engine created successfully" << std::endl;
        
        std::cout << "Creating server on port 12345..." << std::endl;
        Server server(12345, tradingEngine, 4);
        std::cout << "Server created successfully" << std::endl;
        
        std::cout << "Starting server..." << std::endl;
        server.run();
        std::cout << "Server running..." << std::endl;
        
        // Close database connection when done
        DBConnection::close();
    }
    catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

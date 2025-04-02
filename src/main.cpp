#include "server.hpp"
#include "dbConnection.hpp"
#include "tradingEngine.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

int main() {
    try {
        // Setup database connection from environment variables
        std::string host = std::getenv("DB_HOST") ? std::getenv("DB_HOST") : "localhost";
        std::string port = std::getenv("DB_PORT") ? std::getenv("DB_PORT") : "5432";
        std::string user = std::getenv("DB_USER") ? std::getenv("DB_USER") : "exchange";
        std::string password = std::getenv("DB_PASSWORD") ? std::getenv("DB_PASSWORD") : "exchange_password";
        std::string dbname = std::getenv("DB_NAME") ? std::getenv("DB_NAME") : "exchange_db";
        
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
        
        // Create trading engine
        TradingEngine tradingEngine;
        
        // Start server on port 12345
        Server server(12345, tradingEngine);
        server.run();
        
        // Close database connection when done
        DBConnection::close();
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

#include "dbConnection.hpp"
#include <stdexcept>
#include <iostream>

std::unique_ptr<pqxx::connection> DBConnection::conn = nullptr;
std::string DBConnection::connectionString = "";

void DBConnection::initialize(const std::string& connectionString) {
    try {
        std::cout << "Connecting to: " << connectionString << std::endl;
        conn = std::make_unique<pqxx::connection>(connectionString);
        
        if (!conn->is_open()) {
            throw std::runtime_error("Database connection failed: connection not open");
        }
        
        std::cout << "Connected to database successfully: " 
                  << conn->dbname() << " as user " 
                  << conn->username() << std::endl;
    }
    catch (const pqxx::sql_error& e) {
        throw std::runtime_error(std::string("SQL error: ") + e.what() + "\nQuery: " + e.query());
    }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string("Database connection error: ") + e.what());
    }
}

pqxx::connection& DBConnection::getConnection() {
    if (conn == nullptr || !conn->is_open()) {
        initialize(connectionString);
    }
    return *conn;
}

void DBConnection::close() {
    if (conn != nullptr) {
        if (conn->is_open()) {
            // Instead of calling close() directly, we'll reset the unique_ptr
            // which will destroy the connection object and close it
            conn.reset();
        }
    }
}
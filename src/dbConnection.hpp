#ifndef DB_CONNECTION_HPP
#define DB_CONNECTION_HPP

#include <memory>
#include <pqxx/pqxx>
#include <string>

class DBConnection {
private:
    static std::unique_ptr<pqxx::connection> conn;
    static std::string connectionString;

public:
    static void initialize(const std::string& connString);
    static pqxx::connection& getConnection();
    static void close();
};

#endif
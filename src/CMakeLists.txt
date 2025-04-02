cmake_minimum_required(VERSION 3.10)
project(exchange_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# Source files
set(SOURCES
    main.cpp
    server.cpp
    account.cpp
    order.cpp
    tradingEngine.cpp
    accountRepository.cpp
    orderRepository.cpp
    dbConnection.cpp
    threadPool.cpp
    utils.cpp
    tinyxml2.cpp
    orderBook.cpp
)

# Add the executable
add_executable(exchange_server ${SOURCES})

# Find and link libpqxx (this automatically includes PostgreSQL)
find_library(PQXX_LIB pqxx REQUIRED)
find_library(PQ_LIB pq REQUIRED)

# Link libraries
target_link_libraries(exchange_server ${PQXX_LIB} ${PQ_LIB})
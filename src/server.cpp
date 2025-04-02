//src/server.cpp


#include "server.hpp"
#include <netdb.h>
#include <cstring>
#include <string>
#include <iostream>
#include <unistd.h>
#include <csignal>
#include "utils.hpp"
#include <stdexcept>

// Create a global flag for graceful shutdown
static volatile sig_atomic_t running = 1;

Server::Server(int port, TradingEngine& tradingEngine, size_t numThreads) 
    : port(port), serverFd(-1), tradingEngine(tradingEngine), threadPool(numThreads) {
    
    std::cout << "=========== SERVER DEBUG =============" << std::endl;
    std::cout << "Constructor called with port: " << port << std::endl;
    
    try {
        // Register signal handler
        std::cout << "Setting up signal handler..." << std::endl;
        setUpSignalHandler();
        std::cout << "Signal handler set" << std::endl;
        
        // Initialize server socket
        std::cout << "Initializing server socket..." << std::endl;
        init();
        std::cout << "Server socket initialization complete, fd=" << serverFd << std::endl;
        
        if (serverFd < 0) {
            std::cerr << "ERROR: Invalid server socket file descriptor after init()" << std::endl;
            throw std::runtime_error("Server initialization failed, invalid socket");
        }
        
        std::cout << "Server successfully initialized on port " << port << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR creating server: " << e.what() << std::endl;
        throw; // Re-throw to let the caller handle it
    }
}

Server::~Server() {
    if(serverFd != -1) {
        closeServer(serverFd);
    }
}



void Server::init() {
    std::cout << "=== Socket Initialization Debug ===" << std::endl;
    
    // Try a direct socket initialization approach instead of using getaddrinfo
    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        std::cerr << "ERROR: Failed to create socket: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
        throw std::runtime_error("Socket creation failed");
    }
    std::cout << "Socket created with fd=" << serverFd << std::endl;
    
    // Set socket options
    int yes = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        std::cerr << "ERROR: setsockopt failed: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
        close(serverFd);
        serverFd = -1;
        throw std::runtime_error("setsockopt failed");
    }
    std::cout << "Socket options set successfully" << std::endl;
    
    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Accept on any interface
    server_addr.sin_port = htons(port);
    
    // Bind socket
    std::cout << "Binding socket to port " << port << "..." << std::endl;
    if (bind(serverFd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "ERROR: Bind failed: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
        // In Docker, try binding to a specific IP (0.0.0.0)
        std::cout << "Trying alternate bind approach..." << std::endl;
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        
        if (bind(serverFd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "ERROR: Alternative bind also failed: " << strerror(errno) << std::endl;
            close(serverFd);
            serverFd = -1;
            throw std::runtime_error("Bind failed on all attempts");
        }
    }
    std::cout << "Socket bound successfully" << std::endl;
    
    // Listen for connections
    std::cout << "Starting to listen on socket..." << std::endl;
    if (listen(serverFd, SOMAXCONN) < 0) {
        std::cerr << "ERROR: Listen failed: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
        close(serverFd);
        serverFd = -1;
        throw std::runtime_error("Listen failed");
    }
    
    std::cout << "Server listening on port " << port << " with file descriptor " << serverFd << std::endl;
    std::cout << "=== Socket Initialization Complete ===" << std::endl;
    
    // Final check
    if (serverFd < 0) {
        throw std::runtime_error("Socket initialization failed unexpectedly");
    }
}

int Server::acceptconnection(sockaddr_in &clientAddr, socklen_t &clientAddrSize) {
    std::cout << "--- acceptconnection() called ---" << std::endl;
    
    int clientFd;
    
    // Check if server socket is valid before attempting to accept
    if (serverFd < 0) {
        std::cerr << "Cannot accept: server socket is invalid (fd=" << serverFd << ")" << std::endl;
        return -1;
    }
    
    std::cout << "Accepting connection on fd=" << serverFd << "..." << std::endl;
    clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientAddrSize);
    
    if (clientFd == -1) {
        std::cerr << "Accept error (errno=" << errno << "): " << strerror(errno) << std::endl;
        
        if (errno == EINTR) {
            std::cerr << "Accept interrupted by signal" << std::endl;
        } else if (errno == ECONNABORTED || errno == EAGAIN || errno == EWOULDBLOCK) {
            std::cerr << "Accept temporary error" << std::endl;
        } else if (errno == EBADF) {
            std::cerr << "Bad file descriptor detected (fd=" << serverFd << ")" << std::endl;
        }
        
        return -1;
    }
    
    std::cout << "Connection accepted successfully, client fd=" << clientFd << std::endl;
    std::cout << "--- acceptconnection() completed successfully ---" << std::endl;
    return clientFd;
}

void Server::closeServer(int fd){
    if(close(fd) < 0){
        std::cerr << "close error" << std::endl;
        exit(EXIT_FAILURE);
    }
}



void Server::setUpSignalHandler() {
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    std::cout << "Setting up signal handlers..." << std::endl;
    
    if (sigaction(SIGINT, &sa, nullptr) < 0) {
        std::cerr << "SIGINT handler error: " << strerror(errno) << std::endl;
        throw std::runtime_error("Failed to set up SIGINT handler");
    }
    
    if (sigaction(SIGTERM, &sa, nullptr) < 0) {
        std::cerr << "SIGTERM handler error: " << strerror(errno) << std::endl;
        throw std::runtime_error("Failed to set up SIGTERM handler");
    }
    
    std::cout << "Signal handlers configured successfully" << std::endl;
}


void Server::signalHandler(int signum) {
    running = 0;
    const char *msg;
    switch(signum) {
        case SIGINT:
            msg = "Caught Ctrl+C, shutting down gracefully...\n";
            break;
        case SIGTERM:
            msg = "Caught kill command, shutting down...\n";
            break;
        default:
            msg = "Unknown signal, shutting down...\n";
            break;
    }
    write(STDOUT_FILENO, msg, strlen(msg));
    // Don't exit here, allow main loop to exit gracefully
}


void Server::handleClient(int clientFd, TradingEngine &tradingEngine){

    std::string xmlStr;
    if(!readLenAndXML(clientFd, xmlStr)) {
        // read fails
        std::cerr << "Failed to read XML from client or client disconnected" << std::endl;
        closeServer(clientFd);
        return;
    }
    std::string respXML = tradingEngine.processRequest(xmlStr);

    uint32_t length = respXML.size();
    uint32_t netLen = htonl(length);
    ssize_t w1 = write(clientFd, &netLen, sizeof(netLen));
    if(w1 < 0){
        std::cerr << "Failed to write response length" << std::endl;
        closeServer(clientFd);
        return;
    }
    ssize_t w2 = write(clientFd, respXML.c_str(), length);
    if(w2 < 0){
        std::cerr << "Failed to write response content" << std::endl;
        closeServer(clientFd);
        return;
    }
    
    closeServer(clientFd);
}


void Server::run() {
    std::cout << "=== Server Run Debug ===" << std::endl;
    
    // First validate the socket is properly initialized
    if (serverFd < 0) {
        std::cerr << "ERROR: Invalid server socket file descriptor" << std::endl;
        throw std::runtime_error("Invalid server socket file descriptor");
    }
    
    std::cout << "Server fd=" << serverFd << " is valid. Setting up for accept()..." << std::endl;
    struct sockaddr_in clientAddr;
    socklen_t clientAddrSize = sizeof(clientAddr);
    
    std::cout << "Server listening on port " << port << " with file descriptor " << serverFd << std::endl;
    
    // Add the missing accept loop
    while (running) {
        std::cout << "Waiting for connection..." << std::endl;
        int clientFd = acceptconnection(clientAddr, clientAddrSize);
        
        if (clientFd < 0) {
            std::cerr << "Failed to accept connection, waiting before retry..." << std::endl;
            sleep(1); // Wait a moment before retrying
            continue;
        }
        
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::cout << "Accepted connection from " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl;
        
        // Use thread pool to handle client
        threadPool.submit([this, clientFd]() {
            this->handleClient(clientFd, this->tradingEngine);
        });
    }
    
    std::cout << "Server shutting down gracefully" << std::endl;
}



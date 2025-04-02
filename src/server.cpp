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

Server::Server(int port, TradingEngine& engine) 
    : port(port), serverFd(-1), tradingEngine(engine), threadPool(4) {
    
    try {
        // Register signal handler
        signal(SIGINT, signalHandler);
        
        // Initialize server socket
        init();
        
        if (serverFd < 0) {
            throw std::runtime_error("Server initialization failed, invalid socket");
        }
        
        std::cout << "Server successfully initialized on port " << port << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error creating server: " << e.what() << std::endl;
        throw; // Re-throw to let the caller handle it
    }
}

Server::~Server() {
    if(serverFd != -1) {
        closeServer(serverFd);
    }
}



void Server::init() {
    struct addrinfo hints, *serverInfo, *p;
    int status;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // Use my IP

    std::cout << "Initializing server on port " << port << std::endl;

    if ((status = getaddrinfo(NULL, std::to_string(port).c_str(), &hints, &serverInfo)) != 0) {
        std::string error = "getaddrinfo error: " + std::string(gai_strerror(status));
        std::cerr << error << std::endl;
        throw std::runtime_error(error);
    }

    // Loop through all results and bind to the first available
    for (p = serverInfo; p != NULL; p = p->ai_next) {
        serverFd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (serverFd == -1) {
            std::cerr << "socket creation failed: " << strerror(errno) << std::endl;
            continue;
        }

        // Set socket options for reuse
        if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            std::cerr << "setsockopt failed: " << strerror(errno) << std::endl;
            close(serverFd);
            serverFd = -1;
            continue;
        }

        // Bind socket
        if (bind(serverFd, p->ai_addr, p->ai_addrlen) == -1) {
            std::cerr << "bind failed: " << strerror(errno) << std::endl;
            close(serverFd);
            serverFd = -1;
            continue;
        }

        // If we got here, we successfully bound to a socket
        break;
    }

    // Free the linked list
    freeaddrinfo(serverInfo);

    if (p == NULL) {
        throw std::runtime_error("Failed to bind to any address");
    }

    // Start listening
    if (listen(serverFd, SOMAXCONN) == -1) {
        close(serverFd);
        serverFd = -1;
        throw std::runtime_error("listen failed: " + std::string(strerror(errno)));
    }

    std::cout << "Server socket initialized successfully with file descriptor: " << serverFd << std::endl;
}

int Server::acceptconnection(sockaddr_in &clientAddr, socklen_t &clientAddrSize) {
    int clientFd;
    
    // Check if server socket is valid before attempting to accept
    if (serverFd < 0) {
        std::cerr << "Cannot accept: server socket is invalid" << std::endl;
        return -1;
    }
    
    clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientAddrSize);
    
    if (clientFd == -1) {
        if (errno == EINTR) {
            // Interrupted by signal
            std::cerr << "Accept interrupted by signal" << std::endl;
        } else if (errno == ECONNABORTED || errno == EAGAIN || errno == EWOULDBLOCK) {
            // Temporary error
            std::cerr << "Accept temporary error: " << strerror(errno) << std::endl;
        } else {
            // More serious error
            std::cerr << "Accept error: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
            
            // If the socket is bad, try to reinitialize it
            if (errno == EBADF) {
                std::cerr << "Bad file descriptor detected, attempting to reinitialize server socket" << std::endl;
                if (serverFd >= 0) {
                    // Close the old one first if it exists
                    closeServer(serverFd);
                }
                
                try {
                    init(); // Reinitialize the socket
                    std::cout << "Server socket reinitialized with fd: " << serverFd << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "Failed to reinitialize server socket: " << e.what() << std::endl;
                }
            }
        }
        return -1;
    }
    
    return clientFd;
}

void Server::closeServer(int fd){
    if(close(fd) < 0){
        std::cerr << "close error" << std::endl;
        exit(EXIT_FAILURE);
    }
}



void Server::setUpSignalHandler(){
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = Server::signalHandler;
    sa.sa_flags = SA_RESTART;
    //make sure won't block other signals wihle executing signalHandler
    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGINT, &sa, nullptr) < 0){
        std::cerr << "SIGINT handler error" << std::endl;
        exit(EXIT_FAILURE);
    }
    if(sigaction(SIGTERM, &sa, nullptr) < 0){
        std::cerr << "SIGTERM handler error" << std::endl;
        exit(EXIT_FAILURE);
    }

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
    // First validate the socket is properly initialized
    if (serverFd < 0) {
        std::cerr << "ERROR: Invalid server socket file descriptor" << std::endl;
        throw std::runtime_error("Invalid server socket file descriptor");
    }

    struct sockaddr_in clientAddr;
    socklen_t clientAddrSize = sizeof(clientAddr);
    
    std::cout << "Server listening on port " << port << " with file descriptor " << serverFd << std::endl;
    
    while (running) {
        int clientFd = acceptconnection(clientAddr, clientAddrSize);
        
        if (clientFd < 0) {
            // Handle error more gracefully - don't exit immediately
            std::cerr << "Failed to accept connection, waiting before retry..." << std::endl;
            sleep(1); // Wait a moment before retrying
            continue;
        }
        
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::cout << "Accepted connection from " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl;
        
        // Use thread pool to handle client
        threadPool.enqueue([this, clientFd]() {
            this->handleClient(clientFd, this->tradingEngine);
        });
    }
    
    std::cout << "Server shutting down gracefully" << std::endl;
}



#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation error" << std::endl;
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(12345);
    
    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        return -1;
    }
    
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection Failed: " << strerror(errno) << std::endl;
        return -1;
    }
    
    std::cout << "Connected to server successfully" << std::endl;
    
    // Simple test message
    std::string message = "Test message";
    uint32_t length = message.length();
    uint32_t net_length = htonl(length);
    
    // Send length
    send(sock, &net_length, sizeof(net_length), 0);
    
    // Send message
    send(sock, message.c_str(), length, 0);
    
    std::cout << "Message sent" << std::endl;
    
    // Receive response length
    uint32_t resp_length;
    if (read(sock, &resp_length, sizeof(resp_length)) < 0) {
        std::cerr << "Failed to read response length" << std::endl;
        close(sock);
        return -1;
    }
    
    resp_length = ntohl(resp_length);
    
    // Receive response
    char *buffer = new char[resp_length + 1];
    if (read(sock, buffer, resp_length) < 0) {
        std::cerr << "Failed to read response" << std::endl;
        delete[] buffer;
        close(sock);
        return -1;
    }
    
    buffer[resp_length] = '\0';
    std::cout << "Response: " << buffer << std::endl;
    
    delete[] buffer;
    close(sock);
    
    return 0;
}
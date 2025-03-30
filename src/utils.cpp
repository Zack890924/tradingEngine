// src/utils.cpp


#include "utils.hpp"
#include <vector>
#include <cstdint>
#include <arpa/inet.h>

bool read_bytes(int fd, char *buffer, size_t length) {
    size_t totalRead = 0;
    while (totalRead < length) {
        ssize_t bytesRead = read(fd, buffer + totalRead, length - totalRead);
        if (bytesRead <= 0) {
            return false; // Error occurred
        }
        totalRead += bytesRead;
    }
    return totalRead == length; // Return true if all bytes were read
}


bool readLenAndXML(int fd, std::string &xmlStr) {
    uint32_t net_length;
    if(!read_bytes(fd, reinterpret_cast<char*>(&net_length), sizeof(net_length))) {
        return false;
    }
    uint32_t msg_length = ntohl(net_length);
    if(msg_length == 0) {
        return false;
    }
    std::vector<char> buffer(msg_length+1, 0);
    if(!read_bytes(fd, buffer.data(), msg_length)) {
        return false;
    }
    xmlStr.assign(buffer.data(), msg_length);
    return true;
}
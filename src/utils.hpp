#ifndef UTILS_HPP
#define UTILS_HPP

#include <unistd.h>
#include <string>

bool read_bytes(int fd, char *buffer, size_t length);

bool readLenAndXML(int fd, std::string &xmlStr);



#endif // UTILS_HPP
#pragma once

#include <string>

#include <sys/socket.h>

namespace thermal_dtls_gateway {

struct UdpTarget {
    int fd = -1;
    sockaddr_storage addr{};
    socklen_t addrLen = 0;
};

void configureSocketBuffers(int fd, int recvBytes, int sendBytes, const std::string& socketLabel);
int querySocketBufferBytes(int fd, int optName);
std::string socketBufferSummary(int fd);
int bindUdpSocket(const std::string& host, int port);
void configureSocketTimeout(int fd, int seconds);
UdpTarget resolveUdpTarget(const std::string& host, int port);
void disconnectUdpSocket(int fd);
std::string sockaddrToString(const sockaddr_storage& addr);

} // namespace thermal_dtls_gateway

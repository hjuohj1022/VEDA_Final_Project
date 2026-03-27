#include "thermal_dtls_gateway/network_utils.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace thermal_dtls_gateway {

void configureSocketBuffers(int fd, int recvBytes, int sendBytes, const std::string& socketLabel)
{
    if (recvBytes > 0 && ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recvBytes, sizeof(recvBytes)) != 0) {
        std::cerr << "[DTLS] Failed to set " << socketLabel << " SO_RCVBUF: " << std::strerror(errno) << '\n';
    }
    if (sendBytes > 0 && ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendBytes, sizeof(sendBytes)) != 0) {
        std::cerr << "[DTLS] Failed to set " << socketLabel << " SO_SNDBUF: " << std::strerror(errno) << '\n';
    }
}

int querySocketBufferBytes(int fd, int optName)
{
    int value = 0;
    socklen_t len = sizeof(value);
    if (::getsockopt(fd, SOL_SOCKET, optName, &value, &len) != 0) {
        return -1;
    }
    return value;
}

std::string socketBufferSummary(int fd)
{
    std::ostringstream oss;
    oss << "recv=" << querySocketBufferBytes(fd, SO_RCVBUF)
        << " send=" << querySocketBufferBytes(fd, SO_SNDBUF);
    return oss.str();
}

int bindUdpSocket(const std::string& host, int port)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* results = nullptr;
    const std::string portText = std::to_string(port);
    const int rc = getaddrinfo(host.empty() ? nullptr : host.c_str(), portText.c_str(), &hints, &results);
    if (rc != 0) {
        throw std::runtime_error(std::string("getaddrinfo(bind) failed: ") + gai_strerror(rc));
    }

    int fd = -1;
    for (addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }

        int reuse = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

        if (::bind(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen)) == 0) {
            break;
        }

        ::close(fd);
        fd = -1;
    }

    freeaddrinfo(results);

    if (fd < 0) {
        throw std::runtime_error("Failed to bind UDP socket");
    }
    return fd;
}

void configureSocketTimeout(int fd, int seconds)
{
    timeval timeout{};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        throw std::runtime_error(std::string("Failed to set UDP receive timeout: ") + std::strerror(errno));
    }
    if (::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        throw std::runtime_error(std::string("Failed to set UDP send timeout: ") + std::strerror(errno));
    }
}

UdpTarget resolveUdpTarget(const std::string& host, int port)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* results = nullptr;
    const std::string portText = std::to_string(port);
    const int rc = getaddrinfo(host.c_str(), portText.c_str(), &hints, &results);
    if (rc != 0) {
        throw std::runtime_error(std::string("getaddrinfo(forward) failed: ") + gai_strerror(rc));
    }

    UdpTarget target;
    for (addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
        target.fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (target.fd < 0) {
            continue;
        }

        std::memcpy(&target.addr, ai->ai_addr, ai->ai_addrlen);
        target.addrLen = static_cast<socklen_t>(ai->ai_addrlen);
        break;
    }

    freeaddrinfo(results);

    if (target.fd < 0) {
        throw std::runtime_error("Failed to resolve UDP forward target");
    }
    return target;
}

void disconnectUdpSocket(int fd)
{
    sockaddr_storage unspec{};
    auto* addr = reinterpret_cast<sockaddr*>(&unspec);
    addr->sa_family = AF_UNSPEC;
    ::connect(fd, addr, sizeof(sockaddr));
}

std::string sockaddrToString(const sockaddr_storage& addr)
{
    char host[INET6_ADDRSTRLEN] = {0};
    std::uint16_t port = 0;

    if (addr.ss_family == AF_INET) {
        const auto* sin = reinterpret_cast<const sockaddr_in*>(&addr);
        inet_ntop(AF_INET, &sin->sin_addr, host, sizeof(host));
        port = ntohs(sin->sin_port);
    } else if (addr.ss_family == AF_INET6) {
        const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(&addr);
        inet_ntop(AF_INET6, &sin6->sin6_addr, host, sizeof(host));
        port = ntohs(sin6->sin6_port);
    } else {
        return "unknown";
    }

    std::ostringstream oss;
    oss << host << ":" << port;
    return oss.str();
}

} // namespace thermal_dtls_gateway

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

// 운영 중인 소켓의 송수신 버퍼 크기를 조정합니다.
void configureSocketBuffers(int fd, int recvBytes, int sendBytes, const std::string& socketLabel)
{
    if (recvBytes > 0 && ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recvBytes, sizeof(recvBytes)) != 0) {
        std::cerr << "[DTLS] Failed to set " << socketLabel << " SO_RCVBUF: " << std::strerror(errno) << '\n';
    }
    if (sendBytes > 0 && ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendBytes, sizeof(sendBytes)) != 0) {
        std::cerr << "[DTLS] Failed to set " << socketLabel << " SO_SNDBUF: " << std::strerror(errno) << '\n';
    }
}

// 현재 소켓에 실제 적용된 버퍼 크기를 조회합니다.
int querySocketBufferBytes(int fd, int optName)
{
    int value = 0;
    socklen_t len = sizeof(value);
    if (::getsockopt(fd, SOL_SOCKET, optName, &value, &len) != 0) {
        return -1;
    }
    return value;
}

// 소켓 버퍼 상태를 로그용 문자열로 요약합니다.
std::string socketBufferSummary(int fd)
{
    std::ostringstream oss;
    oss << "recv=" << querySocketBufferBytes(fd, SO_RCVBUF)
        << " send=" << querySocketBufferBytes(fd, SO_SNDBUF);
    return oss.str();
}

// host:port에 UDP 소켓을 바인드하고 재사용 옵션도 함께 설정합니다.
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

        // 하나의 주소라도 성공적으로 bind되면 그 소켓을 사용합니다.
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

// recv/send가 영원히 블로킹되지 않도록 UDP 타임아웃을 설정합니다.
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

// Crow Server와 같은 전달 대상의 주소를 해석하고 전용 소켓을 준비합니다.
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

// connect된 UDP 소켓을 다시 비연결 상태로 되돌립니다.
void disconnectUdpSocket(int fd)
{
    sockaddr_storage unspec{};
    auto* addr = reinterpret_cast<sockaddr*>(&unspec);
    addr->sa_family = AF_UNSPEC;
    ::connect(fd, addr, sizeof(sockaddr));
}

// IPv4/IPv6 sockaddr를 사람이 읽기 쉬운 `ip:port` 문자열로 변환합니다.
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

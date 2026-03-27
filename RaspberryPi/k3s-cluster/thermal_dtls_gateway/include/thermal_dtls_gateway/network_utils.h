#pragma once

#include <string>

#include <sys/socket.h>

namespace thermal_dtls_gateway {

// UDP 목적지 소켓과 sockaddr 정보를 함께 들고 다니기 위한 구조체입니다.
struct UdpTarget {
    int fd = -1;
    sockaddr_storage addr{};
    socklen_t addrLen = 0;
};

// 소켓 송수신 버퍼 크기를 조정합니다.
void configureSocketBuffers(int fd, int recvBytes, int sendBytes, const std::string& socketLabel);
// 현재 소켓 버퍼 크기를 조회합니다.
int querySocketBufferBytes(int fd, int optName);
// 송수신 버퍼 정보를 사람이 읽기 쉬운 문자열로 만듭니다.
std::string socketBufferSummary(int fd);
// 지정한 host:port에 UDP 소켓을 바인드합니다.
int bindUdpSocket(const std::string& host, int port);
// UDP 소켓의 송수신 타임아웃을 설정합니다.
void configureSocketTimeout(int fd, int seconds);
// Crow Server 등 외부 전달 대상의 UDP 주소를 해석합니다.
UdpTarget resolveUdpTarget(const std::string& host, int port);
// connect된 UDP 소켓을 다시 비연결 상태로 되돌립니다.
void disconnectUdpSocket(int fd);
// sockaddr를 `ip:port` 형태의 문자열로 변환합니다.
std::string sockaddrToString(const sockaddr_storage& addr);

} // namespace thermal_dtls_gateway

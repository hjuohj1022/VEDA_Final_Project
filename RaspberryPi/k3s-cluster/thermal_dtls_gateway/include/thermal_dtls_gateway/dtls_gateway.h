#pragma once

#include "thermal_dtls_gateway/gateway_constants.h"
#include "thermal_dtls_gateway/network_utils.h"
#include "thermal_dtls_gateway/thermal_protocol.h"

#include <openssl/bio.h>
#include <openssl/ssl.h>

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include <sys/socket.h>

namespace thermal_dtls_gateway {

// DTLS 수신 소켓을 관리하고, 열화상 장치 세션을 받아 Crow Server로
// 복호화된 payload 또는 plain UDP payload를 전달하는 핵심 게이트웨이 클래스입니다.
class DtlsGateway {
public:
    DtlsGateway();
    ~DtlsGateway();

    DtlsGateway(const DtlsGateway&) = delete;
    DtlsGateway& operator=(const DtlsGateway&) = delete;

    // OpenSSL 컨텍스트를 만들고, 수신/전달 소켓을 열어 실행 준비를 마칩니다.
    void init();
    // DTLS 피어와 plain UDP 송신자를 계속 받아들이는 메인 루프입니다.
    void run();

private:
    // 연결된 소켓의 다음 datagram을 미리 확인해 DTLS 처리인지 plain UDP 처리인지 결정합니다.
    IncomingDatagramKind inspectConnectedPeerDatagram(int fd,
                                                      unsigned char* buffer,
                                                      std::size_t bufferLen,
                                                      std::size_t& outLen);
    // plain UDP fallback 세션을 처리하며, 수신한 raw thermal 패킷을 그대로 전달합니다.
    void runRawSessionWorker(int sessionFd, sockaddr_storage peerSock);
    // DTLS handshake를 완료한 뒤 복호화된 application payload를 전달합니다.
    void runSessionWorker(SSL* sessionSsl, int sessionFd, sockaddr_storage peerSock);

    // OpenSSL이 클라이언트 identity에 대응하는 PSK를 조회할 때 사용하는 콜백입니다.
    static unsigned int pskServerCallback(SSL*,
                                          const char* identity,
                                          unsigned char* psk,
                                          unsigned int maxPskLen);
    // DTLS cookie exchange를 사용할 때 OpenSSL이 호출하는 생성/검증 훅입니다.
    static int generateCookie(SSL* ssl, unsigned char* cookie, unsigned int* cookieLen);
    static int verifyCookie(SSL* ssl, const unsigned char* cookie, unsigned int cookieLen);

    // 원격 피어 주소를 기반으로 stateless DTLS cookie를 생성하고 검증합니다.
    int buildCookie(SSL* ssl, unsigned char* cookie, unsigned int* cookieLen);
    int checkCookie(SSL* ssl, const unsigned char* cookie, unsigned int cookieLen);
    // cookie exchange를 쓰지 않을 때 첫 datagram을 기다려 어떤 피어와 연결할지 결정합니다.
    bool waitForPeerDatagram(int fd, sockaddr_storage& out, socklen_t& outLen);
    // cookie HMAC 입력값으로 사용할 피어 주소 바이트를 추출합니다.
    bool cookieMaterial(SSL* ssl, std::vector<unsigned char>& out);
    // OpenSSL의 BIO_ADDR를 일반 sockaddr_storage 형태로 변환합니다.
    static bool bioAddrToSockaddr(const BIO_ADDR* peer, sockaddr_storage& out, socklen_t& outLen);
    // payload 하나를 Crow UDP endpoint로 전송하고 전달 통계를 갱신합니다.
    bool forwardPayload(const unsigned char* data, std::size_t len);
    // 복호화 입력과 전달 출력 양쪽의 패킷/프레임 통계를 기록합니다.
    void recordForwardResult(const unsigned char* data, std::size_t len, bool success);
    // 통계 수집이 켜져 있을 때 주기적으로 요약 로그를 출력합니다.
    void maybeLogStatsLocked(long long nowMs);

    // 환경변수에서 읽어온 바인드/전달 대상 정보입니다.
    std::string bindHost_;
    int bindPort_ = kDefaultDtlsPort;
    std::string forwardHost_;
    int forwardPort_ = kDefaultForwardPort;
    // 프레임 추적, 버퍼 크기, cookie 모드, fallback 동작을 제어하는 실행 옵션입니다.
    bool enableFrameStats_ = false;
    int udpSocketRcvBufBytes_ = kDefaultUdpSocketBufferBytes;
    int udpSocketSndBufBytes_ = kDefaultUdpSocketBufferBytes;
    int statsLogIntervalMs_ = kDefaultStatsLogIntervalMs;
    int frameTrackTimeoutMs_ = kDefaultFrameTrackTimeoutMs;
    int maxTrackedFrames_ = kDefaultMaxTrackedFrames;
    bool allowPlainUdpFallback_ = true;
    bool useCookieExchange_ = false;
    // DTLS-PSK 및 optional cookie 검증에 필요한 보안 재료입니다.
    std::string pskIdentity_;
    std::vector<unsigned char> pskKey_;
    std::vector<unsigned char> cookieSecret_;
    // OpenSSL 상태와 Crow에 도달하기 위한 UDP endpoint 상태입니다.
    SSL_CTX* ctx_ = nullptr;
    int listenFd_ = -1;
    UdpTarget forward_;
    // 세션 worker들이 동시에 통계를 갱신할 수 있으므로 mutex로 보호합니다.
    std::mutex statsMutex_;
    GatewayStats stats_;
};

} // namespace thermal_dtls_gateway

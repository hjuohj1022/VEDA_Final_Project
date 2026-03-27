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

class DtlsGateway {
public:
    DtlsGateway();
    ~DtlsGateway();

    DtlsGateway(const DtlsGateway&) = delete;
    DtlsGateway& operator=(const DtlsGateway&) = delete;

    void init();
    void run();

private:
    IncomingDatagramKind inspectConnectedPeerDatagram(int fd,
                                                      unsigned char* buffer,
                                                      std::size_t bufferLen,
                                                      std::size_t& outLen);
    void runRawSessionWorker(int sessionFd, sockaddr_storage peerSock);
    void runSessionWorker(SSL* sessionSsl, int sessionFd, sockaddr_storage peerSock);

    static unsigned int pskServerCallback(SSL*,
                                          const char* identity,
                                          unsigned char* psk,
                                          unsigned int maxPskLen);
    static int generateCookie(SSL* ssl, unsigned char* cookie, unsigned int* cookieLen);
    static int verifyCookie(SSL* ssl, const unsigned char* cookie, unsigned int cookieLen);

    int buildCookie(SSL* ssl, unsigned char* cookie, unsigned int* cookieLen);
    int checkCookie(SSL* ssl, const unsigned char* cookie, unsigned int cookieLen);
    bool waitForPeerDatagram(int fd, sockaddr_storage& out, socklen_t& outLen);
    bool cookieMaterial(SSL* ssl, std::vector<unsigned char>& out);
    static bool bioAddrToSockaddr(const BIO_ADDR* peer, sockaddr_storage& out, socklen_t& outLen);
    bool forwardPayload(const unsigned char* data, std::size_t len);
    void recordForwardResult(const unsigned char* data, std::size_t len, bool success);
    void maybeLogStatsLocked(long long nowMs);

    std::string bindHost_;
    int bindPort_ = kDefaultDtlsPort;
    std::string forwardHost_;
    int forwardPort_ = kDefaultForwardPort;
    bool enableFrameStats_ = false;
    int udpSocketRcvBufBytes_ = kDefaultUdpSocketBufferBytes;
    int udpSocketSndBufBytes_ = kDefaultUdpSocketBufferBytes;
    int statsLogIntervalMs_ = kDefaultStatsLogIntervalMs;
    int frameTrackTimeoutMs_ = kDefaultFrameTrackTimeoutMs;
    int maxTrackedFrames_ = kDefaultMaxTrackedFrames;
    bool allowPlainUdpFallback_ = true;
    bool useCookieExchange_ = false;
    std::string pskIdentity_;
    std::vector<unsigned char> pskKey_;
    std::vector<unsigned char> cookieSecret_;
    SSL_CTX* ctx_ = nullptr;
    int listenFd_ = -1;
    UdpTarget forward_;
    std::mutex statsMutex_;
    GatewayStats stats_;
};

} // namespace thermal_dtls_gateway

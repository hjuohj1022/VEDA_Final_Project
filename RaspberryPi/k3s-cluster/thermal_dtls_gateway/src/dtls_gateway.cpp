#include "thermal_dtls_gateway/dtls_gateway.h"

#include "thermal_dtls_gateway/gateway_common.h"
#include "thermal_dtls_gateway/network_utils.h"
#include "thermal_dtls_gateway/thermal_protocol.h"

#include <arpa/inet.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

#include <unistd.h>

namespace thermal_dtls_gateway {
namespace {

DtlsGateway* g_gateway = nullptr;

} // namespace

DtlsGateway::DtlsGateway()
    : bindHost_(envOrDefault("DTLS_BIND_HOST", "0.0.0.0"))
    , bindPort_(envIntOrDefault("DTLS_BIND_PORT", kDefaultDtlsPort))
    , forwardHost_(envOrDefault("CROW_FORWARD_HOST", "crow-server-service"))
    , forwardPort_(envIntOrDefault("CROW_FORWARD_PORT", kDefaultForwardPort))
    , enableFrameStats_(envBoolOrDefault("DTLS_ENABLE_FRAME_STATS", false))
    , udpSocketRcvBufBytes_(envIntOrDefault("DTLS_UDP_RCVBUF_BYTES", kDefaultUdpSocketBufferBytes))
    , udpSocketSndBufBytes_(envIntOrDefault("DTLS_UDP_SNDBUF_BYTES", kDefaultUdpSocketBufferBytes))
    , statsLogIntervalMs_(envIntOrDefault("DTLS_STATS_LOG_INTERVAL_MS", kDefaultStatsLogIntervalMs))
    , frameTrackTimeoutMs_(envIntOrDefault("DTLS_FRAME_TRACK_TIMEOUT_MS", kDefaultFrameTrackTimeoutMs))
    , maxTrackedFrames_(envIntOrDefault("DTLS_MAX_TRACKED_FRAMES", kDefaultMaxTrackedFrames))
    , allowPlainUdpFallback_(envBoolOrDefault("DTLS_ALLOW_PLAIN_UDP_FALLBACK", true))
    , useCookieExchange_(envBoolOrDefault("DTLS_USE_COOKIE_EXCHANGE", false))
    , pskIdentity_(requireEnv("DTLS_PSK_IDENTITY"))
    , pskKey_(parseHex(requireEnv("DTLS_PSK_KEY_HEX")))
{
    if (pskKey_.size() < kMinPskBytes) {
        throw std::runtime_error("DTLS_PSK_KEY_HEX must decode to at least 16 bytes");
    }

    if (useCookieExchange_) {
        const char* cookieEnv = std::getenv("DTLS_COOKIE_SECRET_HEX");
        if (cookieEnv != nullptr && !trimCopy(cookieEnv).empty()) {
            cookieSecret_ = parseHex(cookieEnv);
        } else {
            cookieSecret_.resize(32);
            if (RAND_bytes(cookieSecret_.data(), static_cast<int>(cookieSecret_.size())) != 1) {
                throw std::runtime_error("Failed to generate cookie secret");
            }
        }
    }

    g_gateway = this;
}

DtlsGateway::~DtlsGateway()
{
    if (g_gateway == this) {
        g_gateway = nullptr;
    }
    if (listenFd_ >= 0) {
        ::close(listenFd_);
    }
    if (forward_.fd >= 0) {
        ::close(forward_.fd);
    }
    if (ctx_ != nullptr) {
        SSL_CTX_free(ctx_);
    }
}

void DtlsGateway::init()
{
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    ctx_ = SSL_CTX_new(DTLS_server_method());
    if (ctx_ == nullptr) {
        throw std::runtime_error("SSL_CTX_new(DTLS_server_method) failed");
    }

    SSL_CTX_set_min_proto_version(ctx_, DTLS1_2_VERSION);
    SSL_CTX_set_read_ahead(ctx_, 1);
    SSL_CTX_set_psk_server_callback(ctx_, &DtlsGateway::pskServerCallback);
    if (useCookieExchange_) {
        SSL_CTX_set_cookie_generate_cb(ctx_, &DtlsGateway::generateCookie);
        SSL_CTX_set_cookie_verify_cb(ctx_, &DtlsGateway::verifyCookie);
    }
    if (SSL_CTX_use_psk_identity_hint(ctx_, pskIdentity_.c_str()) != 1) {
        throw std::runtime_error("Failed to configure DTLS PSK identity hint");
    }

    if (SSL_CTX_set_cipher_list(ctx_, "PSK-AES128-CCM8:PSK-AES128-GCM-SHA256:PSK-AES128-CBC-SHA256") != 1) {
        throw std::runtime_error("Failed to configure DTLS PSK cipher list");
    }

    listenFd_ = bindUdpSocket(bindHost_, bindPort_);
    configureSocketBuffers(listenFd_, udpSocketRcvBufBytes_, udpSocketSndBufBytes_, "listen socket");
    forward_ = resolveUdpTarget(forwardHost_, forwardPort_);
    configureSocketBuffers(forward_.fd, udpSocketRcvBufBytes_, udpSocketSndBufBytes_, "forward socket");

    std::cout << "[DTLS] Listening on " << bindHost_ << ":" << bindPort_ << '\n';
    std::cout << "[DTLS] Forwarding decrypted thermal UDP to " << forwardHost_ << ":" << forwardPort_ << '\n';
    if (enableFrameStats_) {
        std::cout << "[DTLS] Listen socket buffers " << socketBufferSummary(listenFd_) << '\n';
        std::cout << "[DTLS] Forward socket buffers " << socketBufferSummary(forward_.fd) << '\n';
    }
    if (useCookieExchange_) {
        std::cout << "[DTLS] Using DTLS cookie exchange via DTLSv1_listen\n";
    } else {
        std::cout << "[DTLS] Cookie exchange disabled for proxy-friendly DTLS handshakes\n";
    }
    if (allowPlainUdpFallback_) {
        std::cout << "[DTLS] Plain UDP thermal fallback enabled\n";
    } else {
        std::cout << "[DTLS] Plain UDP thermal fallback disabled\n";
    }
}

void DtlsGateway::run()
{
    const auto reopenListenSocket = [this]() {
        listenFd_ = bindUdpSocket(bindHost_, bindPort_);
        configureSocketBuffers(listenFd_, udpSocketRcvBufBytes_, udpSocketSndBufBytes_, "listen socket");
    };

    while (true) {
        sockaddr_storage peerSock{};
        socklen_t peerLen = 0;
        BIO_ADDR* peer = nullptr;
        const int sessionFd = listenFd_;

        SSL* sessionSsl = SSL_new(ctx_);
        if (sessionSsl == nullptr) {
            throw std::runtime_error("SSL_new failed for DTLS session");
        }

        BIO* sessionBio = BIO_new_dgram(sessionFd, BIO_NOCLOSE);
        if (sessionBio == nullptr) {
            SSL_free(sessionSsl);
            throw std::runtime_error("BIO_new_dgram failed for DTLS session");
        }

        SSL_set_bio(sessionSsl, sessionBio, sessionBio);

        if (useCookieExchange_) {
            peer = BIO_ADDR_new();
            if (peer == nullptr) {
                SSL_free(sessionSsl);
                throw std::runtime_error("BIO_ADDR_new failed");
            }

            ERR_clear_error();
            const int listenRc = DTLSv1_listen(sessionSsl, peer);
            if (listenRc <= 0) {
                printOpenSslErrors("[DTLS] DTLSv1_listen failed");
                SSL_free(sessionSsl);
                BIO_ADDR_free(peer);
                continue;
            }

            if (!bioAddrToSockaddr(peer, peerSock, peerLen)) {
                std::cerr << "[DTLS] Failed to convert peer address\n";
                SSL_free(sessionSsl);
                BIO_ADDR_free(peer);
                continue;
            }
        } else if (!waitForPeerDatagram(sessionFd, peerSock, peerLen)) {
            SSL_free(sessionSsl);
            continue;
        }

        BIO_ADDR_free(peer);
        peer = nullptr;

        bool workerMode = true;
        try {
            reopenListenSocket();
        } catch (const std::exception& ex) {
            std::cerr << "[DTLS] Failed to open replacement listen socket: " << ex.what()
                      << ". Falling back to inline session handling.\n";
            workerMode = false;
        }

        if (::connect(sessionFd, reinterpret_cast<sockaddr*>(&peerSock), peerLen) != 0) {
            std::cerr << "[DTLS] Failed to connect UDP socket to peer: " << std::strerror(errno) << '\n';
            SSL_free(sessionSsl);
            if (!workerMode) {
                disconnectUdpSocket(sessionFd);
            } else {
                ::close(sessionFd);
            }
            continue;
        }

        BIO_ctrl(SSL_get_rbio(sessionSsl), BIO_CTRL_DGRAM_SET_CONNECTED, 0, &peerSock);
        configureSocketTimeout(sessionFd, kHandshakeTimeoutSeconds);

        std::cout << "[DTLS] Peer accepted for handshake: " << sockaddrToString(peerSock) << '\n';

        std::array<unsigned char, kMaxThermalPacketBytes> initialPacket{};
        std::size_t initialPacketLen = 0;
        const IncomingDatagramKind initialKind = inspectConnectedPeerDatagram(sessionFd,
                                                                              initialPacket.data(),
                                                                              initialPacket.size(),
                                                                              initialPacketLen);
        if (initialKind == IncomingDatagramKind::ThermalChunk) {
            ThermalPacketHeader header{};
            (void)looksLikeThermalChunk(initialPacket.data(), initialPacketLen, &header);
            std::cout << "[DTLS] Initial datagram looks like plain thermal UDP"
                      << " peer=" << sockaddrToString(peerSock)
                      << " frame=" << header.frameId
                      << " chunk=" << header.chunkIndex << "/" << header.totalChunks
                      << " bytes=" << initialPacketLen
                      << '\n';

            if (allowPlainUdpFallback_) {
                SSL_free(sessionSsl);
                sessionSsl = nullptr;

                if (workerMode) {
                    try {
                        std::thread(&DtlsGateway::runRawSessionWorker, this, sessionFd, peerSock).detach();
                    } catch (const std::exception& ex) {
                        std::cerr << "[DTLS] Failed to start raw UDP worker: " << ex.what()
                                  << ". Falling back to inline session handling.\n";
                        if (listenFd_ != sessionFd) {
                            ::close(listenFd_);
                            listenFd_ = sessionFd;
                        }
                        runRawSessionWorker(sessionFd, peerSock);
                        reopenListenSocket();
                    }
                    continue;
                }

                runRawSessionWorker(sessionFd, peerSock);
                reopenListenSocket();
                continue;
            }

            std::cerr << "[DTLS] Plain thermal UDP packet received while fallback is disabled. preview="
                      << hexPreview(initialPacket.data(), initialPacketLen) << '\n';
        } else if (initialKind == IncomingDatagramKind::Unknown && initialPacketLen > 0) {
            std::cout << "[DTLS] Initial datagram did not match DTLS or thermal chunk format"
                      << " peer=" << sockaddrToString(peerSock)
                      << " bytes=" << initialPacketLen
                      << " preview=" << hexPreview(initialPacket.data(), initialPacketLen)
                      << '\n';
        }

        if (workerMode) {
            try {
                std::thread(&DtlsGateway::runSessionWorker, this, sessionSsl, sessionFd, peerSock).detach();
            } catch (const std::exception& ex) {
                std::cerr << "[DTLS] Failed to start session worker: " << ex.what()
                          << ". Falling back to inline session handling.\n";
                if (listenFd_ != sessionFd) {
                    ::close(listenFd_);
                    listenFd_ = sessionFd;
                }
                runSessionWorker(sessionSsl, sessionFd, peerSock);
                reopenListenSocket();
            }
            continue;
        }

        runSessionWorker(sessionSsl, sessionFd, peerSock);
        reopenListenSocket();
    }
}

IncomingDatagramKind DtlsGateway::inspectConnectedPeerDatagram(int fd,
                                                               unsigned char* buffer,
                                                               std::size_t bufferLen,
                                                               std::size_t& outLen)
{
    outLen = 0;
    if (fd < 0 || buffer == nullptr || bufferLen == 0) {
        return IncomingDatagramKind::Unknown;
    }

    for (;;) {
        const ssize_t peeked = ::recv(fd, buffer, bufferLen, MSG_PEEK);
        if (peeked >= 0) {
            outLen = static_cast<std::size_t>(peeked);
            return classifyIncomingDatagram(buffer, outLen);
        }

        if (errno == EINTR) {
            continue;
        }
        return IncomingDatagramKind::Unknown;
    }
}

void DtlsGateway::runRawSessionWorker(int sessionFd, sockaddr_storage peerSock)
{
    try {
        const std::string peerText = sockaddrToString(peerSock);
        configureSocketTimeout(sessionFd, kSessionIdleTimeoutSeconds);
        std::cout << "[DTLS] Plain thermal UDP session active for peer " << peerText << '\n';

        std::array<unsigned char, kMaxThermalPacketBytes> buffer{};
        for (;;) {
            const int bytesRead = static_cast<int>(::recv(sessionFd, buffer.data(), buffer.size(), 0));
            if (bytesRead < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::cout << "[DTLS] Plain thermal UDP session timed out for peer " << peerText << '\n';
                } else {
                    std::cerr << "[DTLS] Plain thermal UDP recv failed errno="
                              << errno << " (" << std::strerror(errno) << ")\n";
                }
                break;
            }

            if (bytesRead == 0) {
                continue;
            }

            if (!forwardPayload(buffer.data(), static_cast<std::size_t>(bytesRead))) {
                std::cerr << "[DTLS] Failed to forward plain thermal UDP payload\n";
            }
        }

        ::close(sessionFd);
    } catch (const std::exception& ex) {
        std::cerr << "[DTLS] Plain thermal UDP worker fatal error: " << ex.what() << '\n';
        if (sessionFd >= 0) {
            ::close(sessionFd);
        }
    }
}

void DtlsGateway::runSessionWorker(SSL* sessionSsl, int sessionFd, sockaddr_storage peerSock)
{
    try {
        const std::string peerText = sockaddrToString(peerSock);
        BIO* sessionBio = SSL_get_rbio(sessionSsl);

        ERR_clear_error();
        const int acceptRc = SSL_accept(sessionSsl);
        if (acceptRc <= 0) {
            const int sslError = SSL_get_error(sessionSsl, acceptRc);
            if (sessionBio != nullptr
                && (BIO_dgram_recv_timedout(sessionBio) == 1 || BIO_dgram_send_timedout(sessionBio) == 1)) {
                std::cerr << "[DTLS] SSL_accept timed out for peer " << peerText << '\n';
            } else {
                std::cerr << "[DTLS] SSL_accept failed with sslError=" << sslError
                          << " errno=" << errno << " (" << std::strerror(errno) << ")\n";
                printOpenSslErrors("[DTLS] SSL_accept failed");
            }
            SSL_free(sessionSsl);
            ::close(sessionFd);
            return;
        }

        std::cout << "[DTLS] Handshake complete for peer " << peerText << '\n';
        configureSocketTimeout(sessionFd, kSessionIdleTimeoutSeconds);

        std::array<unsigned char, kMaxThermalPacketBytes> buffer{};
        for (;;) {
            ERR_clear_error();
            const int bytesRead = SSL_read(sessionSsl, buffer.data(), static_cast<int>(buffer.size()));
            if (bytesRead <= 0) {
                const int sslError = SSL_get_error(sessionSsl, bytesRead);
                if (sslError == SSL_ERROR_ZERO_RETURN) {
                    std::cout << "[DTLS] Peer closed session\n";
                } else if (sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE) {
                    continue;
                } else if (sessionBio != nullptr
                           && (BIO_dgram_recv_timedout(sessionBio) == 1 || BIO_dgram_send_timedout(sessionBio) == 1)) {
                    std::cout << "[DTLS] DTLS session timed out for peer " << peerText << '\n';
                } else {
                    printOpenSslErrors("[DTLS] SSL_read failed");
                }
                break;
            }

            if (!forwardPayload(buffer.data(), static_cast<std::size_t>(bytesRead))) {
                std::cerr << "[DTLS] Failed to forward decrypted thermal payload\n";
            }
        }

        SSL_shutdown(sessionSsl);
        SSL_free(sessionSsl);
        ::close(sessionFd);
    } catch (const std::exception& ex) {
        std::cerr << "[DTLS] Session worker fatal error: " << ex.what() << '\n';
        if (sessionSsl != nullptr) {
            SSL_free(sessionSsl);
        }
        if (sessionFd >= 0) {
            ::close(sessionFd);
        }
    }
}

unsigned int DtlsGateway::pskServerCallback(SSL*,
                                            const char* identity,
                                            unsigned char* psk,
                                            unsigned int maxPskLen)
{
    if (g_gateway == nullptr || identity == nullptr) {
        return 0;
    }

    if (g_gateway->pskIdentity_ != identity) {
        std::cerr << "[DTLS] Rejected unknown PSK identity: " << identity << '\n';
        return 0;
    }

    if (g_gateway->pskKey_.size() > maxPskLen) {
        return 0;
    }

    std::memcpy(psk, g_gateway->pskKey_.data(), g_gateway->pskKey_.size());
    return static_cast<unsigned int>(g_gateway->pskKey_.size());
}

int DtlsGateway::generateCookie(SSL* ssl, unsigned char* cookie, unsigned int* cookieLen)
{
    if (g_gateway == nullptr || cookie == nullptr || cookieLen == nullptr) {
        return 0;
    }
    return g_gateway->buildCookie(ssl, cookie, cookieLen);
}

int DtlsGateway::verifyCookie(SSL* ssl, const unsigned char* cookie, unsigned int cookieLen)
{
    if (g_gateway == nullptr || cookie == nullptr) {
        return 0;
    }
    return g_gateway->checkCookie(ssl, cookie, cookieLen);
}

int DtlsGateway::buildCookie(SSL* ssl, unsigned char* cookie, unsigned int* cookieLen)
{
    std::vector<unsigned char> material;
    if (!cookieMaterial(ssl, material)) {
        return 0;
    }

    unsigned int digestLen = 0;
    unsigned char digest[EVP_MAX_MD_SIZE] = {0};
    HMAC(EVP_sha256(),
         cookieSecret_.data(),
         static_cast<int>(cookieSecret_.size()),
         material.data(),
         material.size(),
         digest,
         &digestLen);

    std::memcpy(cookie, digest, digestLen);
    *cookieLen = digestLen;
    return 1;
}

int DtlsGateway::checkCookie(SSL* ssl, const unsigned char* cookie, unsigned int cookieLen)
{
    unsigned char expected[EVP_MAX_MD_SIZE] = {0};
    unsigned int expectedLen = 0;
    if (!buildCookie(ssl, expected, &expectedLen)) {
        return 0;
    }

    if (cookieLen != expectedLen) {
        return 0;
    }

    return CRYPTO_memcmp(cookie, expected, expectedLen) == 0 ? 1 : 0;
}

bool DtlsGateway::waitForPeerDatagram(int fd, sockaddr_storage& out, socklen_t& outLen)
{
    std::array<unsigned char, 1> probe{};

    for (;;) {
        outLen = sizeof(out);
        const ssize_t peeked = ::recvfrom(fd,
                                          probe.data(),
                                          probe.size(),
                                          MSG_PEEK,
                                          reinterpret_cast<sockaddr*>(&out),
                                          &outLen);
        if (peeked >= 0) {
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        std::cerr << "[DTLS] Failed to peek UDP peer: " << std::strerror(errno) << '\n';
        return false;
    }
}

bool DtlsGateway::cookieMaterial(SSL* ssl, std::vector<unsigned char>& out)
{
    sockaddr_storage peer{};
    if (BIO_dgram_get_peer(SSL_get_rbio(ssl), &peer) <= 0) {
        return false;
    }

    out.clear();
    if (peer.ss_family == AF_INET) {
        const auto* sin = reinterpret_cast<const sockaddr_in*>(&peer);
        const auto* portBytes = reinterpret_cast<const unsigned char*>(&sin->sin_port);
        const auto* ipBytes = reinterpret_cast<const unsigned char*>(&sin->sin_addr);
        out.insert(out.end(), portBytes, portBytes + sizeof(sin->sin_port));
        out.insert(out.end(), ipBytes, ipBytes + sizeof(sin->sin_addr));
        return true;
    }

    if (peer.ss_family == AF_INET6) {
        const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(&peer);
        const auto* portBytes = reinterpret_cast<const unsigned char*>(&sin6->sin6_port);
        const auto* ipBytes = reinterpret_cast<const unsigned char*>(&sin6->sin6_addr);
        out.insert(out.end(), portBytes, portBytes + sizeof(sin6->sin6_port));
        out.insert(out.end(), ipBytes, ipBytes + sizeof(sin6->sin6_addr));
        return true;
    }

    return false;
}

bool DtlsGateway::bioAddrToSockaddr(const BIO_ADDR* peer, sockaddr_storage& out, socklen_t& outLen)
{
    if (peer == nullptr) {
        return false;
    }

    std::memset(&out, 0, sizeof(out));
    if (BIO_ADDR_family(peer) == AF_INET) {
        auto* sin = reinterpret_cast<sockaddr_in*>(&out);
        sin->sin_family = AF_INET;
        size_t addrLen = sizeof(sin->sin_addr);
        if (BIO_ADDR_rawaddress(peer, &sin->sin_addr, &addrLen) != 1) {
            return false;
        }
        sin->sin_port = htons(BIO_ADDR_rawport(peer));
        outLen = sizeof(sockaddr_in);
        return true;
    }

    if (BIO_ADDR_family(peer) == AF_INET6) {
        auto* sin6 = reinterpret_cast<sockaddr_in6*>(&out);
        sin6->sin6_family = AF_INET6;
        size_t addrLen = sizeof(sin6->sin6_addr);
        if (BIO_ADDR_rawaddress(peer, &sin6->sin6_addr, &addrLen) != 1) {
            return false;
        }
        sin6->sin6_port = htons(BIO_ADDR_rawport(peer));
        outLen = sizeof(sockaddr_in6);
        return true;
    }

    return false;
}

bool DtlsGateway::forwardPayload(const unsigned char* data, std::size_t len)
{
    const ssize_t sent = ::sendto(forward_.fd,
                                  data,
                                  len,
                                  0,
                                  reinterpret_cast<const sockaddr*>(&forward_.addr),
                                  forward_.addrLen);
    const bool success = sent == static_cast<ssize_t>(len);
    recordForwardResult(data, len, success);
    return success;
}

void DtlsGateway::recordForwardResult(const unsigned char* data, std::size_t len, bool success)
{
    if (!enableFrameStats_) {
        return;
    }

    const long long nowMs = currentTimeMs();
    std::lock_guard<std::mutex> lock(statsMutex_);

    updateThermalPathStats(stats_.decrypted, data, len, nowMs, frameTrackTimeoutMs_, maxTrackedFrames_);
    if (success) {
        updateThermalPathStats(stats_.forwarded, data, len, nowMs, frameTrackTimeoutMs_, maxTrackedFrames_);
    } else {
        stats_.forwardFailures += 1;
        stats_.forwardFailedBytes += static_cast<unsigned long long>(len);
    }

    maybeLogStatsLocked(nowMs);
}

void DtlsGateway::maybeLogStatsLocked(long long nowMs)
{
    if (statsLogIntervalMs_ <= 0) {
        return;
    }
    if (stats_.decrypted.packets == 0 && stats_.forwardFailures == 0) {
        return;
    }
    if (stats_.lastLogAtMs != 0 && (nowMs - stats_.lastLogAtMs) < statsLogIntervalMs_) {
        return;
    }

    stats_.lastLogAtMs = nowMs;
    const ThermalPathStats& lastPath = stats_.forwarded.packets > 0 ? stats_.forwarded : stats_.decrypted;

    std::cout << "[DTLS][STATS] dec_packets=" << stats_.decrypted.packets
              << " dec_bytes=" << stats_.decrypted.bytes
              << " dec_completed=" << stats_.decrypted.completedFrames
              << " dec_incomplete=" << stats_.decrypted.incompleteFrames
              << " dec_missing=" << stats_.decrypted.missingChunks
              << " fwd_packets=" << stats_.forwarded.packets
              << " fwd_bytes=" << stats_.forwarded.bytes
              << " fwd_fail=" << stats_.forwardFailures
              << " fwd_incomplete=" << stats_.forwarded.incompleteFrames
              << " fwd_missing=" << stats_.forwarded.missingChunks
              << " fwd_dup=" << stats_.forwarded.duplicateChunks
              << " inflight=" << stats_.forwarded.inFlightFrames.size()
              << " last_frame=" << lastPath.lastFrameId
              << " last_chunk=" << lastPath.lastChunkIndex << "/" << lastPath.lastTotalChunks
              << '\n';
}

} // namespace thermal_dtls_gateway

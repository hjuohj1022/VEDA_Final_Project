#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int kDefaultDtlsPort = 5005;
constexpr int kDefaultForwardPort = 5005;
constexpr size_t kMaxThermalPacketBytes = 4096;
constexpr int kMinPskBytes = 16;
constexpr int kHandshakeTimeoutSeconds = 15;
constexpr int kSessionIdleTimeoutSeconds = 30;

std::string trimCopy(const std::string& value)
{
    size_t begin = 0;
    size_t end = value.size();

    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string envOrDefault(const char* name, const std::string& fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return fallback;
    }

    const std::string trimmed = trimCopy(value);
    return trimmed.empty() ? fallback : trimmed;
}

std::string requireEnv(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr) {
        throw std::runtime_error(std::string("Missing required environment variable: ") + name);
    }

    const std::string trimmed = trimCopy(value);
    if (trimmed.empty()) {
        throw std::runtime_error(std::string("Environment variable is empty: ") + name);
    }
    return trimmed;
}

int envIntOrDefault(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return fallback;
    }

    const std::string trimmed = trimCopy(value);
    if (trimmed.empty()) {
        return fallback;
    }

    try {
        return std::stoi(trimmed);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("Invalid integer environment variable: ") + name);
    }
}

bool envBoolOrDefault(const char* name, bool fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return fallback;
    }

    std::string trimmed = trimCopy(value);
    for (char& ch : trimmed) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    if (trimmed.empty()) {
        return fallback;
    }
    if (trimmed == "1" || trimmed == "true" || trimmed == "yes" || trimmed == "on") {
        return true;
    }
    if (trimmed == "0" || trimmed == "false" || trimmed == "no" || trimmed == "off") {
        return false;
    }

    throw std::runtime_error(std::string("Invalid boolean environment variable: ") + name);
}

std::vector<unsigned char> parseHex(const std::string& hex)
{
    std::string filtered;
    filtered.reserve(hex.size());
    for (char ch : hex) {
        if (std::isxdigit(static_cast<unsigned char>(ch))) {
            filtered.push_back(ch);
        }
    }

    if (filtered.size() % 2 != 0) {
        throw std::runtime_error("Hex input must contain an even number of digits");
    }

    std::vector<unsigned char> bytes;
    bytes.reserve(filtered.size() / 2);
    for (size_t i = 0; i < filtered.size(); i += 2) {
        const std::string pair = filtered.substr(i, 2);
        bytes.push_back(static_cast<unsigned char>(std::stoul(pair, nullptr, 16)));
    }
    return bytes;
}

void printOpenSslErrors(const std::string& prefix)
{
    std::cerr << prefix << '\n';
    ERR_print_errors_fp(stderr);
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

struct UdpTarget {
    int fd = -1;
    sockaddr_storage addr{};
    socklen_t addrLen = 0;
};

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
    uint16_t port = 0;

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

class DtlsGateway;
DtlsGateway* g_gateway = nullptr;

class DtlsGateway {
public:
    DtlsGateway()
        : bindHost_(envOrDefault("DTLS_BIND_HOST", "0.0.0.0"))
        , bindPort_(envIntOrDefault("DTLS_BIND_PORT", kDefaultDtlsPort))
        , forwardHost_(envOrDefault("CROW_FORWARD_HOST", "crow-server-service"))
        , forwardPort_(envIntOrDefault("CROW_FORWARD_PORT", kDefaultForwardPort))
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
    }

    ~DtlsGateway()
    {
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

    void init()
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
        forward_ = resolveUdpTarget(forwardHost_, forwardPort_);

        std::cout << "[DTLS] Listening on " << bindHost_ << ":" << bindPort_ << '\n';
        std::cout << "[DTLS] Forwarding decrypted thermal UDP to " << forwardHost_ << ":" << forwardPort_ << '\n';
        if (useCookieExchange_) {
            std::cout << "[DTLS] Using DTLS cookie exchange via DTLSv1_listen\n";
        } else {
            std::cout << "[DTLS] Cookie exchange disabled for proxy-friendly DTLS handshakes\n";
        }
    }

    void run()
    {
        while (true) {
            sockaddr_storage peerSock{};
            socklen_t peerLen = 0;
            BIO_ADDR* peer = nullptr;

            SSL* sessionSsl = SSL_new(ctx_);
            if (sessionSsl == nullptr) {
                throw std::runtime_error("SSL_new failed for DTLS session");
            }

            BIO* sessionBio = BIO_new_dgram(listenFd_, BIO_NOCLOSE);
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
            } else if (!waitForPeerDatagram(peerSock, peerLen)) {
                SSL_free(sessionSsl);
                continue;
            }

            if (::connect(listenFd_, reinterpret_cast<sockaddr*>(&peerSock), peerLen) != 0) {
                std::cerr << "[DTLS] Failed to connect UDP socket to peer: " << std::strerror(errno) << '\n';
                SSL_free(sessionSsl);
                BIO_ADDR_free(peer);
                disconnectUdpSocket(listenFd_);
                continue;
            }

            BIO_ctrl(sessionBio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &peerSock);
            configureSocketTimeout(listenFd_, kHandshakeTimeoutSeconds);

            std::cout << "[DTLS] Peer accepted for handshake: " << sockaddrToString(peerSock) << '\n';

            ERR_clear_error();
            const int acceptRc = SSL_accept(sessionSsl);
            if (acceptRc <= 0) {
                const int sslError = SSL_get_error(sessionSsl, acceptRc);
                if (BIO_dgram_recv_timedout(sessionBio) == 1 || BIO_dgram_send_timedout(sessionBio) == 1) {
                    std::cerr << "[DTLS] SSL_accept timed out for peer " << sockaddrToString(peerSock) << '\n';
                } else {
                    std::cerr << "[DTLS] SSL_accept failed with sslError=" << sslError
                              << " errno=" << errno << " (" << std::strerror(errno) << ")\n";
                    printOpenSslErrors("[DTLS] SSL_accept failed");
                }
                SSL_free(sessionSsl);
                BIO_ADDR_free(peer);
                disconnectUdpSocket(listenFd_);
                configureSocketTimeout(listenFd_, 0);
                continue;
            }

            std::cout << "[DTLS] Handshake complete for peer " << sockaddrToString(peerSock) << '\n';
            configureSocketTimeout(listenFd_, kSessionIdleTimeoutSeconds);

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
                    } else if (BIO_dgram_recv_timedout(sessionBio) == 1 || BIO_dgram_send_timedout(sessionBio) == 1) {
                        std::cout << "[DTLS] DTLS session timed out for peer " << sockaddrToString(peerSock) << '\n';
                    } else {
                        printOpenSslErrors("[DTLS] SSL_read failed");
                    }
                    break;
                }

                if (!forwardPayload(buffer.data(), static_cast<size_t>(bytesRead))) {
                    std::cerr << "[DTLS] Failed to forward decrypted thermal payload\n";
                }
            }

            SSL_shutdown(sessionSsl);
            SSL_free(sessionSsl);
            BIO_ADDR_free(peer);
            disconnectUdpSocket(listenFd_);
            configureSocketTimeout(listenFd_, 0);
        }
    }

private:
    static unsigned int pskServerCallback(SSL*, const char* identity, unsigned char* psk, unsigned int maxPskLen)
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

    static int generateCookie(SSL* ssl, unsigned char* cookie, unsigned int* cookieLen)
    {
        if (g_gateway == nullptr || cookie == nullptr || cookieLen == nullptr) {
            return 0;
        }
        return g_gateway->buildCookie(ssl, cookie, cookieLen);
    }

    static int verifyCookie(SSL* ssl, const unsigned char* cookie, unsigned int cookieLen)
    {
        if (g_gateway == nullptr || cookie == nullptr) {
            return 0;
        }
        return g_gateway->checkCookie(ssl, cookie, cookieLen);
    }

    int buildCookie(SSL* ssl, unsigned char* cookie, unsigned int* cookieLen)
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

    int checkCookie(SSL* ssl, const unsigned char* cookie, unsigned int cookieLen)
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

    bool waitForPeerDatagram(sockaddr_storage& out, socklen_t& outLen)
    {
        std::array<unsigned char, 1> probe{};

        for (;;) {
            outLen = sizeof(out);
            const ssize_t peeked = ::recvfrom(listenFd_,
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

    bool cookieMaterial(SSL* ssl, std::vector<unsigned char>& out)
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

    static bool bioAddrToSockaddr(const BIO_ADDR* peer, sockaddr_storage& out, socklen_t& outLen)
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

    bool forwardPayload(const unsigned char* data, size_t len)
    {
        const ssize_t sent = ::sendto(forward_.fd,
                                      data,
                                      len,
                                      0,
                                      reinterpret_cast<const sockaddr*>(&forward_.addr),
                                      forward_.addrLen);
        return sent == static_cast<ssize_t>(len);
    }

    std::string bindHost_;
    int bindPort_ = kDefaultDtlsPort;
    std::string forwardHost_;
    int forwardPort_ = kDefaultForwardPort;
    bool useCookieExchange_ = false;
    std::string pskIdentity_;
    std::vector<unsigned char> pskKey_;
    std::vector<unsigned char> cookieSecret_;
    SSL_CTX* ctx_ = nullptr;
    int listenFd_ = -1;
    UdpTarget forward_;
};
} // namespace

int main()
{
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    std::cout << "[DTLS] Gateway process starting" << std::endl;

    try {
        DtlsGateway gateway;
        g_gateway = &gateway;
        gateway.init();
        gateway.run();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[DTLS] Fatal error: " << ex.what() << '\n';
        printOpenSslErrors("[DTLS] OpenSSL error stack");
        return 1;
    }
}

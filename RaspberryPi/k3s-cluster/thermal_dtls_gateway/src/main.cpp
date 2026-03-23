#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int kDefaultDtlsPort = 5005;
constexpr int kDefaultForwardPort = 5005;
constexpr size_t kMaxThermalPacketBytes = 4096;
constexpr size_t kThermalHeaderBytes = 10;
constexpr size_t kDtlsRecordHeaderBytes = 13;
constexpr int kMinPskBytes = 16;
constexpr int kHandshakeTimeoutSeconds = 15;
constexpr int kSessionIdleTimeoutSeconds = 30;
constexpr int kDefaultUdpSocketBufferBytes = 2 * 1024 * 1024;
constexpr int kDefaultStatsLogIntervalMs = 5000;
constexpr int kDefaultFrameTrackTimeoutMs = 2000;
constexpr int kDefaultMaxTrackedFrames = 8;

long long currentTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

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

uint16_t readBe16(const unsigned char* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
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

struct UdpTarget {
    int fd = -1;
    sockaddr_storage addr{};
    socklen_t addrLen = 0;
};

struct ThermalPacketHeader {
    uint16_t frameId = 0;
    uint16_t chunkIndex = 0;
    uint16_t totalChunks = 0;
    uint16_t minValue = 0;
    uint16_t maxValue = 0;
};

struct ThermalFrameTracker {
    uint16_t totalChunks = 0;
    long long firstSeenAtMs = 0;
    long long lastSeenAtMs = 0;
    std::set<uint16_t> uniqueChunks;
};

struct ThermalPathStats {
    unsigned long long packets = 0;
    unsigned long long bytes = 0;
    unsigned long long invalidPackets = 0;
    unsigned long long duplicateChunks = 0;
    unsigned long long completedFrames = 0;
    unsigned long long incompleteFrames = 0;
    unsigned long long missingChunks = 0;
    unsigned long long evictedFrames = 0;
    uint16_t lastFrameId = 0;
    uint16_t lastChunkIndex = 0;
    uint16_t lastTotalChunks = 0;
    size_t lastPacketBytes = 0;
    long long lastPacketAtMs = 0;
    size_t maxInFlightFrames = 0;
    std::map<uint16_t, ThermalFrameTracker> inFlightFrames;
};

struct GatewayStats {
    ThermalPathStats decrypted;
    ThermalPathStats forwarded;
    unsigned long long forwardFailures = 0;
    unsigned long long forwardFailedBytes = 0;
    long long lastLogAtMs = 0;
};

enum class IncomingDatagramKind {
    DtlsRecord,
    ThermalChunk,
    Unknown,
};

bool parseThermalPacketHeader(const unsigned char* data, size_t len, ThermalPacketHeader& out)
{
    if (data == nullptr || len < kThermalHeaderBytes) {
        return false;
    }

    out.frameId = readBe16(data + 0);
    out.chunkIndex = readBe16(data + 2);
    out.totalChunks = readBe16(data + 4);
    out.minValue = readBe16(data + 6);
    out.maxValue = readBe16(data + 8);
    return true;
}

bool isReasonableThermalPacketHeader(const ThermalPacketHeader& header)
{
    return header.totalChunks > 0 &&
           header.totalChunks <= 100 &&
           header.chunkIndex < header.totalChunks;
}

bool looksLikeDtlsRecord(const unsigned char* data, size_t len)
{
    if (data == nullptr || len < kDtlsRecordHeaderBytes) {
        return false;
    }

    const unsigned char contentType = data[0];
    const bool validContentType =
        contentType == 20 || // change_cipher_spec
        contentType == 21 || // alert
        contentType == 22 || // handshake
        contentType == 23 || // application_data
        contentType == 24;   // heartbeat
    if (!validContentType) {
        return false;
    }

    const bool validVersion =
        (data[1] == 0xFE) &&
        (data[2] == 0xFD || data[2] == 0xFF);
    if (!validVersion) {
        return false;
    }

    const size_t recordLen = (static_cast<size_t>(data[11]) << 8U) |
                             static_cast<size_t>(data[12]);
    return recordLen > 0;
}

bool looksLikeThermalChunk(const unsigned char* data, size_t len, ThermalPacketHeader* headerOut = nullptr)
{
    ThermalPacketHeader header{};
    if (!parseThermalPacketHeader(data, len, header) || !isReasonableThermalPacketHeader(header)) {
        return false;
    }

    if (headerOut != nullptr) {
        *headerOut = header;
    }
    return true;
}

IncomingDatagramKind classifyIncomingDatagram(const unsigned char* data,
                                              size_t len,
                                              ThermalPacketHeader* thermalHeaderOut = nullptr)
{
    if (looksLikeDtlsRecord(data, len)) {
        return IncomingDatagramKind::DtlsRecord;
    }
    if (looksLikeThermalChunk(data, len, thermalHeaderOut)) {
        return IncomingDatagramKind::ThermalChunk;
    }
    return IncomingDatagramKind::Unknown;
}

std::string hexPreview(const unsigned char* data, size_t len, size_t maxBytes = 16)
{
    if (data == nullptr || len == 0) {
        return "(empty)";
    }

    std::ostringstream oss;
    const size_t previewLen = std::min(len, maxBytes);
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < previewLen; ++i) {
        if (i != 0) {
            oss << ' ';
        }
        oss << std::setw(2) << static_cast<unsigned int>(data[i]);
    }
    if (len > previewLen) {
        oss << " ...";
    }
    return oss.str();
}

void noteIncompleteFrame(ThermalPathStats& stats, const ThermalFrameTracker& tracker)
{
    stats.incompleteFrames += 1;
    if (tracker.totalChunks > tracker.uniqueChunks.size()) {
        stats.missingChunks += static_cast<unsigned long long>(tracker.totalChunks - tracker.uniqueChunks.size());
    }
}

void pruneExpiredFrames(ThermalPathStats& stats, long long nowMs, int timeoutMs)
{
    if (timeoutMs <= 0) {
        return;
    }

    for (auto it = stats.inFlightFrames.begin(); it != stats.inFlightFrames.end();) {
        if ((nowMs - it->second.lastSeenAtMs) <= timeoutMs) {
            ++it;
            continue;
        }

        noteIncompleteFrame(stats, it->second);
        it = stats.inFlightFrames.erase(it);
    }
}

void trimTrackedFrames(ThermalPathStats& stats, int maxTrackedFrames)
{
    if (maxTrackedFrames <= 0) {
        return;
    }

    while (static_cast<int>(stats.inFlightFrames.size()) > maxTrackedFrames) {
        const auto oldest = std::min_element(stats.inFlightFrames.begin(),
                                             stats.inFlightFrames.end(),
                                             [](const auto& lhs, const auto& rhs) {
                                                 return lhs.second.lastSeenAtMs < rhs.second.lastSeenAtMs;
                                             });
        if (oldest == stats.inFlightFrames.end()) {
            return;
        }

        noteIncompleteFrame(stats, oldest->second);
        stats.evictedFrames += 1;
        stats.inFlightFrames.erase(oldest);
    }
}

void updateThermalPathStats(ThermalPathStats& stats,
                            const unsigned char* data,
                            size_t len,
                            long long nowMs,
                            int frameTimeoutMs,
                            int maxTrackedFrames)
{
    stats.packets += 1;
    stats.bytes += static_cast<unsigned long long>(len);
    stats.lastPacketBytes = len;
    stats.lastPacketAtMs = nowMs;

    ThermalPacketHeader header{};
    if (!parseThermalPacketHeader(data, len, header)) {
        stats.invalidPackets += 1;
        return;
    }

    stats.lastFrameId = header.frameId;
    stats.lastChunkIndex = header.chunkIndex;
    stats.lastTotalChunks = header.totalChunks;

    pruneExpiredFrames(stats, nowMs, frameTimeoutMs);

    ThermalFrameTracker& tracker = stats.inFlightFrames[header.frameId];
    if (tracker.firstSeenAtMs == 0) {
        tracker.firstSeenAtMs = nowMs;
        tracker.totalChunks = header.totalChunks;
    }
    tracker.lastSeenAtMs = nowMs;
    if (header.totalChunks > tracker.totalChunks) {
        tracker.totalChunks = header.totalChunks;
    }

    const uint16_t minimumChunks = static_cast<uint16_t>(header.chunkIndex + 1);
    if (minimumChunks > tracker.totalChunks) {
        tracker.totalChunks = minimumChunks;
    }

    if (!tracker.uniqueChunks.insert(header.chunkIndex).second) {
        stats.duplicateChunks += 1;
    }

    if (tracker.totalChunks > 0 && tracker.uniqueChunks.size() >= tracker.totalChunks) {
        stats.completedFrames += 1;
        stats.inFlightFrames.erase(header.frameId);
    }

    trimTrackedFrames(stats, maxTrackedFrames);
    stats.maxInFlightFrames = std::max(stats.maxInFlightFrames, stats.inFlightFrames.size());
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

    void run()
    {
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
                listenFd_ = bindUdpSocket(bindHost_, bindPort_);
                configureSocketBuffers(listenFd_, udpSocketRcvBufBytes_, udpSocketSndBufBytes_, "listen socket");
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
            size_t initialPacketLen = 0;
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
                            listenFd_ = bindUdpSocket(bindHost_, bindPort_);
                            configureSocketBuffers(listenFd_, udpSocketRcvBufBytes_, udpSocketSndBufBytes_, "listen socket");
                        }
                        continue;
                    }

                    runRawSessionWorker(sessionFd, peerSock);
                    listenFd_ = bindUdpSocket(bindHost_, bindPort_);
                    configureSocketBuffers(listenFd_, udpSocketRcvBufBytes_, udpSocketSndBufBytes_, "listen socket");
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
                    listenFd_ = bindUdpSocket(bindHost_, bindPort_);
                    configureSocketBuffers(listenFd_, udpSocketRcvBufBytes_, udpSocketSndBufBytes_, "listen socket");
                }
                continue;
            }

            runSessionWorker(sessionSsl, sessionFd, peerSock);
            listenFd_ = bindUdpSocket(bindHost_, bindPort_);
            configureSocketBuffers(listenFd_, udpSocketRcvBufBytes_, udpSocketSndBufBytes_, "listen socket");
        }
    }

private:
    IncomingDatagramKind inspectConnectedPeerDatagram(int fd,
                                                      unsigned char* buffer,
                                                      size_t bufferLen,
                                                      size_t& outLen)
    {
        outLen = 0;
        if (fd < 0 || buffer == nullptr || bufferLen == 0) {
            return IncomingDatagramKind::Unknown;
        }

        for (;;) {
            const ssize_t peeked = ::recv(fd, buffer, bufferLen, MSG_PEEK);
            if (peeked >= 0) {
                outLen = static_cast<size_t>(peeked);
                return classifyIncomingDatagram(buffer, outLen);
            }

            if (errno == EINTR) {
                continue;
            }
            return IncomingDatagramKind::Unknown;
        }
    }

    void runRawSessionWorker(int sessionFd, sockaddr_storage peerSock)
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

                if (!forwardPayload(buffer.data(), static_cast<size_t>(bytesRead))) {
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

    void runSessionWorker(SSL* sessionSsl, int sessionFd, sockaddr_storage peerSock)
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

                if (!forwardPayload(buffer.data(), static_cast<size_t>(bytesRead))) {
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

    bool waitForPeerDatagram(int fd, sockaddr_storage& out, socklen_t& outLen)
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
        const bool success = sent == static_cast<ssize_t>(len);
        recordForwardResult(data, len, success);
        return success;
    }

    void recordForwardResult(const unsigned char* data, size_t len, bool success)
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

    void maybeLogStatsLocked(long long nowMs)
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

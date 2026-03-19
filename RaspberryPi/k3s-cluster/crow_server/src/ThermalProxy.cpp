#include "../include/ThermalProxy.h"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// main.cpp exposes JWT verification as a process-wide helper.
bool verifyJWT(const std::string& token);

namespace {
constexpr uint16_t kDefaultThermalUdpPort = 5005;
constexpr size_t kThermalHeaderBytes = 10;
constexpr int kThermalWidth = 160;
constexpr int kThermalHeight = 120;
constexpr int kThermalFrameBytes = kThermalWidth * kThermalHeight * 2;
constexpr size_t kMaxUdpPacketBytes = 64 * 1024;
constexpr int kReceiveTimeoutUs = 250000;
constexpr int kDefaultUdpSocketBufferBytes = 2 * 1024 * 1024;
constexpr int kDefaultStatsLogIntervalMs = 5000;
constexpr int kDefaultFrameTrackTimeoutMs = 2000;
constexpr int kDefaultMaxTrackedFrames = 8;

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

struct ThermalProxyStats {
    bool udp_bound = false;
    bool receiver_running = false;
    std::string udp_bind_host = "0.0.0.0";
    uint16_t udp_port = kDefaultThermalUdpPort;
    int udp_socket_rcvbuf_bytes = 0;
    int ws_clients = 0;
    uint16_t last_frame_id = 0;
    uint16_t last_chunk_index = 0;
    uint16_t last_total_chunks = 0;
    uint16_t last_min_val = 0;
    uint16_t last_max_val = 0;
    std::string last_sender;
    size_t last_packet_bytes = 0;
    long long last_packet_at_ms = 0;
    unsigned long long packets_received = 0;
    unsigned long long bytes_received = 0;
    unsigned long long invalid_packets = 0;
    unsigned long long duplicate_chunks = 0;
    unsigned long long completed_frames = 0;
    unsigned long long incomplete_frames = 0;
    unsigned long long missing_chunks = 0;
    unsigned long long evicted_frames = 0;
    size_t in_flight_frames = 0;
    size_t max_in_flight_frames = 0;
    std::string last_error;
};

struct ThermalProxyState {
    std::mutex state_mutex;
    std::mutex receiver_mutex;
    std::set<crow::websocket::connection*> clients;
    std::thread receiver_thread;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> receiver_running{false};
    std::atomic<bool> receiver_thread_started{false};
    ThermalProxyStats stats;
    std::map<uint16_t, ThermalFrameTracker> in_flight_frames;
    long long last_stats_log_at_ms = 0;
};

ThermalProxyState g_thermal;

long long currentTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string envOrDefault(const char* key, const std::string& fallback)
{
    const char* value = std::getenv(key);
    return value ? std::string(value) : fallback;
}

uint16_t envPortOrDefault(const char* key, uint16_t fallback)
{
    const char* value = std::getenv(key);
    if (!value || !*value) {
        return fallback;
    }

    try {
        const int parsed = std::stoi(value);
        if (parsed <= 0 || parsed > 65535) {
            return fallback;
        }
        return static_cast<uint16_t>(parsed);
    } catch (const std::exception&) {
        return fallback;
    }
}

int envIntOrDefault(const char* key, int fallback)
{
    const char* value = std::getenv(key);
    if (!value || !*value) {
        return fallback;
    }

    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return fallback;
    }
}

uint16_t readBe16(const unsigned char* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

bool parseThermalPacketHeader(const std::string& payload, ThermalPacketHeader& out)
{
    if (payload.size() < kThermalHeaderBytes) {
        return false;
    }

    const unsigned char* p = reinterpret_cast<const unsigned char*>(payload.data());
    out.frameId = readBe16(p + 0);
    out.chunkIndex = readBe16(p + 2);
    out.totalChunks = readBe16(p + 4);
    out.minValue = readBe16(p + 6);
    out.maxValue = readBe16(p + 8);
    return true;
}

std::string senderToString(const sockaddr_in& sender)
{
    char host[INET_ADDRSTRLEN] = {0};
    if (::inet_ntop(AF_INET, &sender.sin_addr, host, sizeof(host)) == nullptr) {
        return "unknown";
    }

    return std::string(host) + ":" + std::to_string(ntohs(sender.sin_port));
}

void configureReceiveBuffer(int fd, int bytes)
{
    if (bytes <= 0) {
        return;
    }

    if (::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) != 0) {
        std::cerr << "[THERMAL] setsockopt(SO_RCVBUF) failed: " << std::strerror(errno) << std::endl;
    }
}

int querySocketBufferBytes(int fd, int optName)
{
    int value = 0;
    socklen_t value_len = sizeof(value);
    if (::getsockopt(fd, SOL_SOCKET, optName, &value, &value_len) != 0) {
        return -1;
    }
    return value;
}

std::string extractBearerToken(const crow::request& req)
{
    const std::string auth_header = req.get_header_value("Authorization");
    if (auth_header.size() <= 7 || auth_header.rfind("Bearer ", 0) != 0) {
        return {};
    }
    return auth_header.substr(7);
}

bool isAuthorized(const crow::request& req)
{
    const std::string token = extractBearerToken(req);
    return !token.empty() && verifyJWT(token);
}

std::vector<crow::websocket::connection*> snapshotClients()
{
    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
    return std::vector<crow::websocket::connection*>(g_thermal.clients.begin(), g_thermal.clients.end());
}

void setReceiverState(bool running, bool bound, const std::string& bind_host, uint16_t port, const std::string& error = {})
{
    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
    g_thermal.stats.receiver_running = running;
    g_thermal.stats.udp_bound = bound;
    g_thermal.stats.udp_bind_host = bind_host;
    g_thermal.stats.udp_port = port;
    g_thermal.stats.last_error = error;
    g_thermal.receiver_running.store(running);
}

void noteIncompleteFrameLocked(uint16_t frameId, const ThermalFrameTracker& tracker, const char* reason)
{
    g_thermal.stats.incomplete_frames += 1;
    if (tracker.totalChunks > tracker.uniqueChunks.size()) {
        g_thermal.stats.missing_chunks += static_cast<unsigned long long>(tracker.totalChunks - tracker.uniqueChunks.size());
    }
    if (std::strcmp(reason, "drop incomplete frame") == 0) {
        g_thermal.stats.evicted_frames += 1;
    }

    std::cout << "[THERMAL] " << reason
              << " id= " << frameId
              << " chunks= " << tracker.uniqueChunks.size() << " / " << tracker.totalChunks
              << std::endl;
}

void pruneExpiredFramesLocked(long long nowMs, int timeoutMs)
{
    if (timeoutMs <= 0) {
        return;
    }

    for (auto it = g_thermal.in_flight_frames.begin(); it != g_thermal.in_flight_frames.end();) {
        if ((nowMs - it->second.lastSeenAtMs) <= timeoutMs) {
            ++it;
            continue;
        }

        noteIncompleteFrameLocked(it->first, it->second, "frame timeout");
        it = g_thermal.in_flight_frames.erase(it);
    }
}

void trimTrackedFramesLocked(int maxTrackedFrames)
{
    if (maxTrackedFrames <= 0) {
        return;
    }

    while (static_cast<int>(g_thermal.in_flight_frames.size()) > maxTrackedFrames) {
        const auto oldest = std::min_element(g_thermal.in_flight_frames.begin(),
                                             g_thermal.in_flight_frames.end(),
                                             [](const auto& lhs, const auto& rhs) {
                                                 return lhs.second.lastSeenAtMs < rhs.second.lastSeenAtMs;
                                             });
        if (oldest == g_thermal.in_flight_frames.end()) {
            return;
        }

        noteIncompleteFrameLocked(oldest->first, oldest->second, "drop incomplete frame");
        g_thermal.in_flight_frames.erase(oldest);
    }
}

void maybeLogThermalStatsLocked(long long nowMs, int statsLogIntervalMs)
{
    if (statsLogIntervalMs <= 0 || g_thermal.stats.packets_received == 0) {
        return;
    }
    if (g_thermal.last_stats_log_at_ms != 0 && (nowMs - g_thermal.last_stats_log_at_ms) < statsLogIntervalMs) {
        return;
    }

    g_thermal.last_stats_log_at_ms = nowMs;
    std::cout << "[THERMAL][STATS] packets=" << g_thermal.stats.packets_received
              << " bytes=" << g_thermal.stats.bytes_received
              << " completed=" << g_thermal.stats.completed_frames
              << " incomplete=" << g_thermal.stats.incomplete_frames
              << " missing=" << g_thermal.stats.missing_chunks
              << " duplicate=" << g_thermal.stats.duplicate_chunks
              << " invalid=" << g_thermal.stats.invalid_packets
              << " inflight=" << g_thermal.stats.in_flight_frames
              << " last_frame=" << g_thermal.stats.last_frame_id
              << " last_chunk=" << g_thermal.stats.last_chunk_index << "/" << g_thermal.stats.last_total_chunks
              << " last_sender=" << g_thermal.stats.last_sender
              << std::endl;
}

void updatePacketStats(const std::string& payload,
                       const std::string& senderText,
                       int frameTimeoutMs,
                       int maxTrackedFrames,
                       int statsLogIntervalMs)
{
    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
    const long long nowMs = currentTimeMs();

    g_thermal.stats.packets_received += 1;
    g_thermal.stats.bytes_received += static_cast<unsigned long long>(payload.size());
    g_thermal.stats.last_packet_bytes = payload.size();
    g_thermal.stats.last_packet_at_ms = nowMs;
    g_thermal.stats.last_sender = senderText;

    pruneExpiredFramesLocked(nowMs, frameTimeoutMs);
    g_thermal.stats.in_flight_frames = g_thermal.in_flight_frames.size();
    g_thermal.stats.max_in_flight_frames = std::max(g_thermal.stats.max_in_flight_frames, g_thermal.stats.in_flight_frames);

    ThermalPacketHeader header{};
    if (!parseThermalPacketHeader(payload, header)) {
        g_thermal.stats.invalid_packets += 1;
        maybeLogThermalStatsLocked(nowMs, statsLogIntervalMs);
        return;
    }

    g_thermal.stats.last_frame_id = header.frameId;
    g_thermal.stats.last_chunk_index = header.chunkIndex;
    g_thermal.stats.last_total_chunks = header.totalChunks;
    g_thermal.stats.last_min_val = header.minValue;
    g_thermal.stats.last_max_val = header.maxValue;

    ThermalFrameTracker& tracker = g_thermal.in_flight_frames[header.frameId];
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
        g_thermal.stats.duplicate_chunks += 1;
    }

    if (tracker.totalChunks > 0 && tracker.uniqueChunks.size() >= tracker.totalChunks) {
        g_thermal.stats.completed_frames += 1;
        g_thermal.in_flight_frames.erase(header.frameId);
    }

    trimTrackedFramesLocked(maxTrackedFrames);
    g_thermal.stats.in_flight_frames = g_thermal.in_flight_frames.size();
    g_thermal.stats.max_in_flight_frames = std::max(g_thermal.stats.max_in_flight_frames, g_thermal.stats.in_flight_frames);

    maybeLogThermalStatsLocked(nowMs, statsLogIntervalMs);
}

void clearThermalFrameTrackers()
{
    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
    g_thermal.in_flight_frames.clear();
    g_thermal.last_stats_log_at_ms = 0;
    g_thermal.stats.in_flight_frames = 0;

    g_thermal.stats.last_frame_id = 0;
    g_thermal.stats.last_chunk_index = 0;
    g_thermal.stats.last_total_chunks = 0;
    g_thermal.stats.last_min_val = 0;
    g_thermal.stats.last_max_val = 0;
    g_thermal.stats.last_sender.clear();
    g_thermal.stats.last_packet_bytes = 0;
    g_thermal.stats.last_packet_at_ms = 0;
    g_thermal.stats.packets_received = 0;
    g_thermal.stats.bytes_received = 0;
    g_thermal.stats.invalid_packets = 0;
    g_thermal.stats.duplicate_chunks = 0;
    g_thermal.stats.completed_frames = 0;
    g_thermal.stats.incomplete_frames = 0;
    g_thermal.stats.missing_chunks = 0;
    g_thermal.stats.evicted_frames = 0;
    g_thermal.stats.max_in_flight_frames = 0;
    g_thermal.stats.udp_socket_rcvbuf_bytes = 0;
}

void broadcastThermalChunk(const std::string& payload)
{
    const auto clients = snapshotClients();
    for (auto* client : clients) {
        if (!client) {
            continue;
        }
        client->send_binary(payload);
    }
}

crow::json::wvalue makeThermalStatusJson()
{
    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);

    crow::json::wvalue response;
    response["status"] = "ok";
    response["stream"] = "thermal16";
    response["udp_bound"] = g_thermal.stats.udp_bound;
    response["receiver_running"] = g_thermal.stats.receiver_running;
    response["udp_bind_host"] = g_thermal.stats.udp_bind_host;
    response["udp_port"] = g_thermal.stats.udp_port;
    response["udp_socket_rcvbuf_bytes"] = g_thermal.stats.udp_socket_rcvbuf_bytes;
    response["ws_clients"] = g_thermal.stats.ws_clients;
    response["last_frame_id"] = static_cast<int>(g_thermal.stats.last_frame_id);
    response["last_chunk_index"] = static_cast<int>(g_thermal.stats.last_chunk_index);
    response["last_total_chunks"] = static_cast<int>(g_thermal.stats.last_total_chunks);
    response["last_min_val"] = static_cast<int>(g_thermal.stats.last_min_val);
    response["last_max_val"] = static_cast<int>(g_thermal.stats.last_max_val);
    response["last_sender"] = g_thermal.stats.last_sender;
    response["last_packet_bytes"] = static_cast<int>(g_thermal.stats.last_packet_bytes);
    response["last_packet_at_ms"] = g_thermal.stats.last_packet_at_ms;
    response["packets_received"] = static_cast<uint64_t>(g_thermal.stats.packets_received);
    response["bytes_received"] = static_cast<uint64_t>(g_thermal.stats.bytes_received);
    response["invalid_packets"] = static_cast<uint64_t>(g_thermal.stats.invalid_packets);
    response["duplicate_chunks"] = static_cast<uint64_t>(g_thermal.stats.duplicate_chunks);
    response["completed_frames"] = static_cast<uint64_t>(g_thermal.stats.completed_frames);
    response["incomplete_frames"] = static_cast<uint64_t>(g_thermal.stats.incomplete_frames);
    response["missing_chunks"] = static_cast<uint64_t>(g_thermal.stats.missing_chunks);
    response["evicted_frames"] = static_cast<uint64_t>(g_thermal.stats.evicted_frames);
    response["in_flight_frames"] = static_cast<int>(g_thermal.stats.in_flight_frames);
    response["max_in_flight_frames"] = static_cast<int>(g_thermal.stats.max_in_flight_frames);
    response["last_error"] = g_thermal.stats.last_error;
    response["format"]["width"] = kThermalWidth;
    response["format"]["height"] = kThermalHeight;
    response["format"]["pixel"] = "u16be";
    response["format"]["header_bytes"] = static_cast<int>(kThermalHeaderBytes);
    response["format"]["frame_bytes"] = kThermalFrameBytes;
    return response;
}

void thermalReceiverLoop()
{
    const std::string bind_host = envOrDefault("THERMAL_UDP_BIND_HOST", "0.0.0.0");
    const uint16_t bind_port = envPortOrDefault("THERMAL_UDP_PORT", kDefaultThermalUdpPort);
    const int receive_buffer_bytes = envIntOrDefault("THERMAL_UDP_RCVBUF_BYTES", kDefaultUdpSocketBufferBytes);
    const int frame_timeout_ms = envIntOrDefault("THERMAL_FRAME_TRACK_TIMEOUT_MS", kDefaultFrameTrackTimeoutMs);
    const int max_tracked_frames = envIntOrDefault("THERMAL_MAX_TRACKED_FRAMES", kDefaultMaxTrackedFrames);
    const int stats_log_interval_ms = envIntOrDefault("THERMAL_STATS_LOG_INTERVAL_MS", kDefaultStatsLogIntervalMs);

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        const std::string error = std::string("socket() failed: ") + std::strerror(errno);
        std::cerr << "[THERMAL] " << error << std::endl;
        setReceiverState(false, false, bind_host, bind_port, error);
        g_thermal.receiver_thread_started.store(false);
        return;
    }

    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        std::cerr << "[THERMAL] setsockopt(SO_REUSEADDR) failed: " << std::strerror(errno) << std::endl;
    }
    configureReceiveBuffer(fd, receive_buffer_bytes);

    timeval timeout {};
    timeout.tv_sec = 0;
    timeout.tv_usec = kReceiveTimeoutUs;
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        std::cerr << "[THERMAL] setsockopt(SO_RCVTIMEO) failed: " << std::strerror(errno) << std::endl;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(bind_port);

    if (bind_host == "*" || bind_host == "0.0.0.0" || bind_host.empty()) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, bind_host.c_str(), &addr.sin_addr) != 1) {
        const std::string error = "invalid THERMAL_UDP_BIND_HOST: " + bind_host;
        std::cerr << "[THERMAL] " << error << std::endl;
        ::close(fd);
        setReceiverState(false, false, bind_host, bind_port, error);
        g_thermal.receiver_thread_started.store(false);
        return;
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const std::string error = std::string("bind() failed: ") + std::strerror(errno);
        std::cerr << "[THERMAL] " << error << " host=" << bind_host << " port=" << bind_port << std::endl;
        ::close(fd);
        setReceiverState(false, false, bind_host, bind_port, error);
        g_thermal.receiver_thread_started.store(false);
        return;
    }

    clearThermalFrameTrackers();
    {
        std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
        g_thermal.stats.udp_socket_rcvbuf_bytes = querySocketBufferBytes(fd, SO_RCVBUF);
    }
    setReceiverState(true, true, bind_host, bind_port);
    std::cout << "[THERMAL] UDP receiver bound to " << bind_host << ":" << bind_port
              << " recvbuf=" << querySocketBufferBytes(fd, SO_RCVBUF)
              << std::endl;

    std::vector<char> buffer(kMaxUdpPacketBytes);
    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_thermal.receiver_mutex);
            if (g_thermal.stop_requested.load()) {
                break;
            }
        }

        sockaddr_in sender {};
        socklen_t sender_len = sizeof(sender);
        const ssize_t received = ::recvfrom(fd,
                                            buffer.data(),
                                            buffer.size(),
                                            0,
                                            reinterpret_cast<sockaddr*>(&sender),
                                            &sender_len);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }

            const std::string error = std::string("recvfrom() failed: ") + std::strerror(errno);
            std::cerr << "[THERMAL] " << error << std::endl;
            setReceiverState(true, true, bind_host, bind_port, error);
            continue;
        }

        if (received < static_cast<ssize_t>(kThermalHeaderBytes)) {
            continue;
        }

        const std::string payload(buffer.data(), static_cast<size_t>(received));
        updatePacketStats(payload,
                          senderToString(sender),
                          frame_timeout_ms,
                          max_tracked_frames,
                          stats_log_interval_ms);
        broadcastThermalChunk(payload);
    }

    ::close(fd);
    setReceiverState(false, false, bind_host, bind_port);
    g_thermal.receiver_thread_started.store(false);
    std::cout << "[THERMAL] UDP receiver stopped" << std::endl;
}

void ensureThermalReceiverRunning()
{
    std::lock_guard<std::mutex> lock(g_thermal.receiver_mutex);
    if (g_thermal.receiver_thread_started.load()) {
        return;
    }
    if (g_thermal.receiver_thread.joinable()) {
        g_thermal.receiver_thread.join();
    }

    g_thermal.stop_requested.store(false);
    g_thermal.receiver_thread_started.store(true);
    g_thermal.receiver_thread = std::thread(thermalReceiverLoop);
}

void requestThermalReceiverStopIfIdle()
{
    std::lock_guard<std::mutex> receiver_lock(g_thermal.receiver_mutex);
    std::lock_guard<std::mutex> state_lock(g_thermal.state_mutex);
    if (!g_thermal.clients.empty()) {
        return;
    }
    g_thermal.stop_requested.store(true);
}
} // namespace

void registerThermalProxyRoutes(crow::SimpleApp& app)
{
    CROW_ROUTE(app, "/thermal/control/start").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req) {
        if (!isAuthorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        ensureThermalReceiverRunning();

        crow::json::wvalue response;
        response["status"] = "accepted";
        response["stream"] = "thermal16";
        response["transport"] = "websocket";
        response["ws_path"] = "/thermal/stream";
        response["udp_bind_host"] = envOrDefault("THERMAL_UDP_BIND_HOST", "0.0.0.0");
        response["udp_port"] = envPortOrDefault("THERMAL_UDP_PORT", kDefaultThermalUdpPort);
        response["format"]["width"] = kThermalWidth;
        response["format"]["height"] = kThermalHeight;
        response["format"]["pixel"] = "u16be";
        response["format"]["header_bytes"] = static_cast<int>(kThermalHeaderBytes);
        response["format"]["frame_bytes"] = kThermalFrameBytes;
        return crow::response(202, response);
    });

    CROW_ROUTE(app, "/thermal/control/stop").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req) {
        if (!isAuthorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        requestThermalReceiverStopIfIdle();

        crow::json::wvalue response;
        response["status"] = "accepted";
        response["note"] = "receiver stops when no WebSocket clients remain";
        return crow::response(202, response);
    });

    CROW_ROUTE(app, "/thermal/status")
    ([](const crow::request& req) {
        if (!isAuthorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        return crow::response(makeThermalStatusJson());
    });

    CROW_WEBSOCKET_ROUTE(app, "/thermal/stream")
        .onaccept([](const crow::request& req, void**) {
            return isAuthorized(req);
        })
        .onopen([](crow::websocket::connection& conn) {
            ensureThermalReceiverRunning();

            std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
            g_thermal.clients.insert(&conn);
            g_thermal.stats.ws_clients = static_cast<int>(g_thermal.clients.size());
        })
        .onclose([](crow::websocket::connection& conn, const std::string&) {
            {
                std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
                g_thermal.clients.erase(&conn);
                g_thermal.stats.ws_clients = static_cast<int>(g_thermal.clients.size());
            }
            requestThermalReceiverStopIfIdle();
        })
        .onmessage([](crow::websocket::connection&, const std::string&, bool) {
            // The thermal stream is server -> client only.
        });
}

void shutdownThermalProxy()
{
    {
        std::lock_guard<std::mutex> lock(g_thermal.receiver_mutex);
        g_thermal.stop_requested.store(true);
    }

    if (g_thermal.receiver_thread.joinable()) {
        g_thermal.receiver_thread.join();
    }
    g_thermal.receiver_thread_started.store(false);
}

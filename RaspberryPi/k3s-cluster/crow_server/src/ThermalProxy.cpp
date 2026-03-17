#include "../include/ThermalProxy.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
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

struct ThermalProxyStats {
    bool udp_bound = false;
    bool receiver_running = false;
    std::string udp_bind_host = "0.0.0.0";
    uint16_t udp_port = kDefaultThermalUdpPort;
    int ws_clients = 0;
    uint16_t last_frame_id = 0;
    uint16_t last_chunk_index = 0;
    uint16_t last_total_chunks = 0;
    uint16_t last_min_val = 0;
    uint16_t last_max_val = 0;
    size_t last_packet_bytes = 0;
    long long last_packet_at_ms = 0;
    unsigned long long packets_received = 0;
    unsigned long long bytes_received = 0;
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

uint16_t readBe16(const unsigned char* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
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

void updatePacketStats(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
    g_thermal.stats.packets_received += 1;
    g_thermal.stats.bytes_received += static_cast<unsigned long long>(payload.size());
    g_thermal.stats.last_packet_bytes = payload.size();
    g_thermal.stats.last_packet_at_ms = currentTimeMs();

    if (payload.size() >= kThermalHeaderBytes) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(payload.data());
        g_thermal.stats.last_frame_id = readBe16(p + 0);
        g_thermal.stats.last_chunk_index = readBe16(p + 2);
        g_thermal.stats.last_total_chunks = readBe16(p + 4);
        g_thermal.stats.last_min_val = readBe16(p + 6);
        g_thermal.stats.last_max_val = readBe16(p + 8);
    }
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
    response["ws_clients"] = g_thermal.stats.ws_clients;
    response["last_frame_id"] = static_cast<int>(g_thermal.stats.last_frame_id);
    response["last_chunk_index"] = static_cast<int>(g_thermal.stats.last_chunk_index);
    response["last_total_chunks"] = static_cast<int>(g_thermal.stats.last_total_chunks);
    response["last_min_val"] = static_cast<int>(g_thermal.stats.last_min_val);
    response["last_max_val"] = static_cast<int>(g_thermal.stats.last_max_val);
    response["last_packet_bytes"] = static_cast<int>(g_thermal.stats.last_packet_bytes);
    response["last_packet_at_ms"] = g_thermal.stats.last_packet_at_ms;
    response["packets_received"] = static_cast<uint64_t>(g_thermal.stats.packets_received);
    response["bytes_received"] = static_cast<uint64_t>(g_thermal.stats.bytes_received);
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

    setReceiverState(true, true, bind_host, bind_port);
    std::cout << "[THERMAL] UDP receiver bound to " << bind_host << ":" << bind_port << std::endl;

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
        updatePacketStats(payload);
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
        .onaccept([](const crow::request& req) {
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

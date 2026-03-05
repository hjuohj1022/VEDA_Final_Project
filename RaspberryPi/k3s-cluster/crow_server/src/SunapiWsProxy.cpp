#include "../include/SunapiWsProxy.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {

struct WsBridge {
    std::unique_ptr<asio::io_context> ioc;
    std::unique_ptr<websocket::stream<tcp::socket>> upstream;
    std::thread readerThread;
    std::mutex writeMutex;
    std::atomic<bool> stopped{false};
};

std::mutex g_bridgeMutex;
std::unordered_map<crow::websocket::connection*, std::shared_ptr<WsBridge>> g_bridges;

std::string envOrDefault(const char* key, const std::string& def) {
    const char* v = std::getenv(key);
    return v ? std::string(v) : def;
}

std::string trimTrailingSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

bool parseWsUrl(const std::string& url, std::string& scheme, std::string& host, std::string& port, std::string& target) {
    static const std::regex re(R"(^(ws|wss)://([^/:]+)(?::([0-9]+))?(\/.*)?$)", std::regex::icase);
    std::smatch m;
    if (!std::regex_match(url, m, re)) return false;
    scheme = m[1].str();
    host = m[2].str();
    port = m[3].matched ? m[3].str() : (scheme == "wss" || scheme == "WSS" ? "443" : "80");
    target = m[4].matched ? m[4].str() : "/StreamingServer";
    if (target.empty()) target = "/StreamingServer";
    return true;
}

std::string resolveUpstreamWsUrl() {
    const std::string wsUrl = envOrDefault("SUNAPI_WS_URL", "");
    if (!wsUrl.empty()) return wsUrl;

    const std::string base = trimTrailingSlash(envOrDefault("SUNAPI_BASE_URL", ""));
    if (base.empty()) return {};
    if (base.rfind("http://", 0) == 0) return "ws://" + base.substr(7) + "/StreamingServer";
    if (base.rfind("https://", 0) == 0) return "wss://" + base.substr(8) + "/StreamingServer";
    return {};
}

void eraseBridge(crow::websocket::connection* conn) {
    std::lock_guard<std::mutex> lock(g_bridgeMutex);
    g_bridges.erase(conn);
}

void stopBridge(crow::websocket::connection* conn) {
    std::shared_ptr<WsBridge> bridge;
    {
        std::lock_guard<std::mutex> lock(g_bridgeMutex);
        auto it = g_bridges.find(conn);
        if (it == g_bridges.end()) return;
        bridge = it->second;
        g_bridges.erase(it);
    }

    if (!bridge) return;
    bridge->stopped = true;
    if (bridge->upstream) {
        beast::error_code ec;
        bridge->upstream->close(websocket::close_code::normal, ec);
    }
    if (bridge->readerThread.joinable()) {
        bridge->readerThread.join();
    }
}

std::shared_ptr<WsBridge> getBridge(crow::websocket::connection* conn) {
    std::lock_guard<std::mutex> lock(g_bridgeMutex);
    auto it = g_bridges.find(conn);
    return it == g_bridges.end() ? nullptr : it->second;
}

} // namespace

void registerSunapiWsProxyRoutes(crow::SimpleApp& app) {
    CROW_WEBSOCKET_ROUTE(app, "/sunapi/StreamingServer")
        .onopen([](crow::websocket::connection& conn) {
            const std::string upstreamUrl = resolveUpstreamWsUrl();
            if (upstreamUrl.empty()) {
                conn.send_text("SUNAPI_WS_URL or SUNAPI_BASE_URL is not configured");
                conn.close("upstream not configured");
                return;
            }

            std::string scheme, host, port, target;
            if (!parseWsUrl(upstreamUrl, scheme, host, port, target)) {
                conn.send_text("invalid SUNAPI_WS_URL");
                conn.close("invalid upstream url");
                return;
            }

            if (scheme == "wss" || scheme == "WSS") {
                // 현재 버전은 ws만 지원. 필요 시 TLS(ws over ssl) 확장 가능.
                conn.send_text("wss upstream is not supported in current build");
                conn.close("unsupported upstream scheme");
                return;
            }

            auto bridge = std::make_shared<WsBridge>();
            bridge->ioc = std::make_unique<asio::io_context>();
            bridge->upstream = std::make_unique<websocket::stream<tcp::socket>>(*bridge->ioc);

            try {
                tcp::resolver resolver(*bridge->ioc);
                auto const results = resolver.resolve(host, port);
                asio::connect(bridge->upstream->next_layer(), results.begin(), results.end());
                bridge->upstream->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                bridge->upstream->handshake(host + ":" + port, target);
            } catch (const std::exception& e) {
                std::cerr << "[SUNAPI_WS_PROXY] upstream connect failed: " << e.what() << std::endl;
                conn.send_text(std::string("upstream connect failed: ") + e.what());
                conn.close("upstream connect failed");
                return;
            }

            {
                std::lock_guard<std::mutex> lock(g_bridgeMutex);
                g_bridges[&conn] = bridge;
            }

            bridge->readerThread = std::thread([bridge, &conn]() {
                try {
                    while (!bridge->stopped) {
                        beast::flat_buffer buffer;
                        bridge->upstream->read(buffer);
                        const auto payload = beast::buffers_to_string(buffer.cdata());
                        conn.send_binary(payload);
                    }
                } catch (const std::exception& e) {
                    if (!bridge->stopped) {
                        std::cerr << "[SUNAPI_WS_PROXY] read loop ended: " << e.what() << std::endl;
                        conn.close("upstream disconnected");
                    }
                }
            });
        })
        .onmessage([](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
            auto bridge = getBridge(&conn);
            if (!bridge || !bridge->upstream || bridge->stopped) return;

            try {
                std::lock_guard<std::mutex> lock(bridge->writeMutex);
                bridge->upstream->binary(is_binary);
                bridge->upstream->write(asio::buffer(data.data(), data.size()));
            } catch (const std::exception& e) {
                std::cerr << "[SUNAPI_WS_PROXY] write failed: " << e.what() << std::endl;
                conn.close("upstream write failed");
                stopBridge(&conn);
            }
        })
        .onclose([](crow::websocket::connection& conn, const std::string&) {
            stopBridge(&conn);
            eraseBridge(&conn);
        });
}


#include "../include/SunapiWsProxy.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
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

#include <openssl/ssl.h>

// Crow WebSocket과 SUNAPI StreamingServer 사이의 직접 중계부.
// 클라이언트 하나당 업스트림 연결 하나, reader thread 하나의 대응 구조.
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {

// Crow WebSocket 클라이언트 1개 대응 업스트림 연결 수명 및 입출력 상태 보관.
struct WsBridge {
    std::unique_ptr<asio::io_context> ioc;
    std::unique_ptr<asio::ssl::context> sslCtx;
    std::unique_ptr<websocket::stream<tcp::socket>> upstreamWs;
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> upstreamWss;
    std::thread readerThread;
    std::mutex writeMutex;
    std::atomic<bool> stopped{false};
};

std::mutex g_bridgeMutex;
std::unordered_map<crow::websocket::connection*, std::shared_ptr<WsBridge>> g_bridges;

// SUNAPI WebSocket 주소의 전용 환경변수 또는 SUNAPI_BASE_URL 기반 유도.
std::string envOrDefault(const char* key, const std::string& def) {
    const char* v = std::getenv(key);
    return v ? std::string(v) : def;
}

bool envToBool(const char* key, bool defaultValue) {
    const char* value = std::getenv(key);
    if (!value) return defaultValue;
    std::string s(value);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return (s == "1" || s == "true" || s == "yes" || s == "on");
}

std::string trimTrailingSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

bool parseWsUrl(const std::string& url, std::string& scheme, std::string& host, std::string& port, std::string& target) {
    // ws://host:port/path 형태의 Crow 처리용 조각 분리.
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
    // onclose, write 실패, 업스트림 read 실패의 공통 정리 경로.
    // stopped 플래그 설정 및 업스트림/reader thread 정리 담당.
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

    beast::error_code ec;
    if (bridge->upstreamWs) {
        bridge->upstreamWs->close(websocket::close_code::normal, ec);
    }
    if (bridge->upstreamWss) {
        bridge->upstreamWss->close(websocket::close_code::normal, ec);
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

}  // 익명 네임스페이스

void registerSunapiWsProxyRoutes(crow::SimpleApp& app) {
    CROW_WEBSOCKET_ROUTE(app, "/sunapi/StreamingServer")
        .onopen([](crow::websocket::connection& conn) {
            // 클라이언트 접속 시 업스트림 SUNAPI WebSocket 동시 연결.
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

            std::string schemeLower = scheme;
            std::transform(schemeLower.begin(), schemeLower.end(), schemeLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const bool useTls = (schemeLower == "wss");

            auto bridge = std::make_shared<WsBridge>();
            bridge->ioc = std::make_unique<asio::io_context>();

            try {
                tcp::resolver resolver(*bridge->ioc);
                auto const results = resolver.resolve(host, port);

                if (useTls) {
                    const bool insecure = envToBool("SUNAPI_INSECURE", true);
                    bridge->sslCtx = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_client);
                    bridge->sslCtx->set_verify_mode(insecure ? asio::ssl::verify_none : asio::ssl::verify_peer);

                    if (!insecure) {
                        beast::error_code ecVerify;
                        bridge->sslCtx->set_default_verify_paths(ecVerify);
                    }

                    bridge->upstreamWss = std::make_unique<websocket::stream<beast::ssl_stream<tcp::socket>>>(*bridge->ioc, *bridge->sslCtx);
                    asio::connect(beast::get_lowest_layer(*bridge->upstreamWss), results.begin(), results.end());

                    if (!SSL_set_tlsext_host_name(bridge->upstreamWss->next_layer().native_handle(), host.c_str())) {
                        throw std::runtime_error("failed to set TLS SNI");
                    }

                    bridge->upstreamWss->next_layer().handshake(asio::ssl::stream_base::client);
                    bridge->upstreamWss->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                    bridge->upstreamWss->handshake(host + ":" + port, target);
                } else {
                    bridge->upstreamWs = std::make_unique<websocket::stream<tcp::socket>>(*bridge->ioc);
                    asio::connect(bridge->upstreamWs->next_layer(), results.begin(), results.end());
                    bridge->upstreamWs->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                    bridge->upstreamWs->handshake(host + ":" + port, target);
                }
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
                // 업스트림 수신 프레임의 가공 없는 Crow WebSocket 전달.
                try {
                    while (!bridge->stopped) {
                        beast::flat_buffer buffer;
                        if (bridge->upstreamWss) {
                            bridge->upstreamWss->read(buffer);
                        } else if (bridge->upstreamWs) {
                            bridge->upstreamWs->read(buffer);
                        } else {
                            break;
                        }
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
            if (!bridge || bridge->stopped) return;
            if (!bridge->upstreamWs && !bridge->upstreamWss) return;

            try {
                // 업스트림 write의 writeMutex 기반 직렬화, 프레임 순서 및 thread 안전성 확보.
                std::lock_guard<std::mutex> lock(bridge->writeMutex);
                if (bridge->upstreamWss) {
                    bridge->upstreamWss->binary(is_binary);
                    bridge->upstreamWss->write(asio::buffer(data.data(), data.size()));
                } else {
                    bridge->upstreamWs->binary(is_binary);
                    bridge->upstreamWs->write(asio::buffer(data.data(), data.size()));
                }
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

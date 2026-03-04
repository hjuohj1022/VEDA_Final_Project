#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <filesystem>
#include <utility>
#include <vector>

// Windows networking
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// 설정 파일 포함 (.gitignore로 관리됨)
#include "app_config.h"
#include "command_dispatcher.h"
#include "logging.h"
#include "request.h"
#include "runtime_config.h"
#include "server_runtime.h"

namespace {
std::string ResolveConfigPath(const std::string& configuredPath) {
    namespace fs = std::filesystem;
    fs::path p(configuredPath);
    if (p.is_absolute()) return p.string();

    std::error_code ec;
    fs::path cwdCandidate = fs::current_path() / p;
    if (fs::exists(cwdCandidate, ec)) return cwdCandidate.string();

    fs::path base = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        fs::path candidate = base / p;
        if (fs::exists(candidate, ec)) return candidate.string();
        if (!base.has_parent_path()) break;
        base = base.parent_path();
    }
    return configuredPath;
}

bool ValidateStartupConfig(const RuntimeConfig& cfg, int port, const TlsServerConfig& tlsCfg, std::string& outErr) {
    if (port <= 0 || port > 65535) {
        outErr = "[CFG] invalid --port value: " + std::to_string(port) + " (expected 1..65535)";
        return false;
    }
    if (cfg.server_listen_backlog <= 0) {
        outErr = "[CFG] invalid server_listen_backlog: " + std::to_string(cfg.server_listen_backlog);
        return false;
    }

    if (!tlsCfg.enabled) return true;

    const std::vector<std::pair<std::string, std::string>> requiredFiles = {
        {"control_tls.ca_file", tlsCfg.caFile},
        {"control_tls.cert_file", tlsCfg.certFile},
        {"control_tls.key_file", tlsCfg.keyFile},
    };
    for (const auto& item : requiredFiles) {
        const std::string resolved = ResolveConfigPath(item.second);
        std::error_code ec;
        if (!std::filesystem::exists(resolved, ec)) {
            outErr = "[CFG] missing file: " + item.first + "=" + item.second;
            return false;
        }
    }
    if (tlsCfg.sslDll.empty() || tlsCfg.cryptoDll.empty()) {
        outErr = "[CFG] control_tls ssl/crypto dll name must not be empty";
        return false;
    }
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    const RuntimeConfig& cfg = GetRuntimeConfig();
    int port = 9090;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--port=", 0) == 0) {
            port = std::stoi(arg.substr(7));
        }
    }

    TlsServerConfig tlsCfg;
    tlsCfg.enabled = cfg.control_tls.enabled;
    tlsCfg.requireClientCert = cfg.control_tls.require_client_cert;
    tlsCfg.caFile = cfg.control_tls.ca_file;
    tlsCfg.certFile = cfg.control_tls.cert_file;
    tlsCfg.keyFile = cfg.control_tls.key_file;
    tlsCfg.sslDll = cfg.control_tls.ssl_dll;
    tlsCfg.cryptoDll = cfg.control_tls.crypto_dll;

    std::string validationErr;
    if (!ValidateStartupConfig(cfg, port, tlsCfg, validationErr)) {
        LogError(validationErr);
        return 1;
    }

    ServerSocketContext serverCtx;
    if (!InitServerSocket(port, cfg.server_listen_backlog, &tlsCfg, serverCtx)) {
        return 1;
    }

    LogInfo("Listening on port " + std::to_string(port) +
            (cfg.control_tls.enabled ? " (mTLS)" : " (plain TCP)"));

    std::thread worker;
    std::thread streamThread;
    std::atomic<bool> workerStop{false};
    DepthStreamBuffer depthStream;
    std::thread rgbdStreamThread;
    RgbdStreamBuffer rgbdStream;
    bool workerRunning = false;
    std::atomic<bool> streamActive{false};
    std::atomic<bool> rgbdStreamActive{false};
    std::thread pcStreamThread;
    ImageStreamBuffer pcStream;
    std::atomic<bool> pcStreamActive{false};
    ViewParams viewParams;
    ServerRuntimeContext runtimeCtx{
        worker,
        streamThread,
        rgbdStreamThread,
        pcStreamThread,
        workerStop,
        streamActive,
        rgbdStreamActive,
        pcStreamActive,
        workerRunning,
        depthStream,
        rgbdStream,
        pcStream,
        viewParams,
    };

    while (true) {
        JoinFinishedStreamThreads(runtimeCtx);
        sockaddr_in clientAddr{};
        int clientLen = sizeof(clientAddr);
        ServerClient client;
        if (!AcceptServerClient(serverCtx, client, &clientAddr, &clientLen)) continue;

        char buf[1024];
        int len = ClientRecv(client, buf, sizeof(buf) - 1);
        if (len <= 0) {
            const ClientIoErrorInfo ioErr = GetLastClientIoError();
            if (ioErr.kind == ClientIoErrorKind::kClosed) {
                LogWarn("Client disconnected before request");
            } else {
                LogWarn("Client recv failed: " + ioErr.detail);
            }
            CloseServerClient(client);
            continue;
        }
        buf[len] = '\0';

        std::string line(buf);
        {
            char ip[64] = {0};
            inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
            LogInfo(std::string("Request from ") + ip + ":" +
                    std::to_string(ntohs(clientAddr.sin_port)) + " -> " + line);
        }
        Request req = ParseRequest(line);
        HandleClientRequest(client, req, runtimeCtx);
    }

    ShutdownRuntime(runtimeCtx);
    ShutdownServerSocket(serverCtx);
    return 0;
}

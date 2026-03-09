#include <array>
#include <cstddef>
#include <string>
#include <thread>
#include <atomic>
#include <filesystem>
#include <string_view>
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
constexpr int kDefaultPort = 9090;
constexpr int kMinPort = 1;
constexpr int kMaxPort = 65535;
constexpr std::size_t kClientBufferSize = 1024U;
constexpr int kConfigSearchDepth = 6;

std::string ResolveConfigPath(const std::string& configuredPath) {
    namespace fs = std::filesystem;
    fs::path p(configuredPath);
    if (p.is_absolute()) {
        return p.string();
    }

    std::error_code ec;
    fs::path cwdCandidate = fs::current_path() / p;
    if (fs::exists(cwdCandidate, ec)) {
        return cwdCandidate.string();
    }

    fs::path base = fs::current_path();
    for (int i = 0; i < kConfigSearchDepth; ++i) {
        fs::path candidate = base / p;
        if (fs::exists(candidate, ec)) {
            return candidate.string();
        }
        if (!base.has_parent_path()) {
            break;
        }
        base = base.parent_path();
    }
    return configuredPath;
}

bool ParsePortArgument(const std::string& arg, int& outPort, std::string& outErr) {
    constexpr std::string_view kPortPrefix = "--port=";
    if (arg.rfind(kPortPrefix.data(), 0) != 0) {
        return true;
    }

    int parsedPort = 0;
    if (!ParseInt(arg.substr(kPortPrefix.size()), parsedPort)) {
        outErr = "[CFG] invalid --port value: " + arg.substr(kPortPrefix.size());
        return false;
    }

    outPort = parsedPort;
    return true;
}

TlsServerConfig BuildTlsServerConfig(const RuntimeConfig& cfg) {
    TlsServerConfig tlsCfg;
    tlsCfg.enabled = cfg.control_tls.enabled;
    tlsCfg.requireClientCert = cfg.control_tls.require_client_cert;
    tlsCfg.caFile = cfg.control_tls.ca_file;
    tlsCfg.certFile = cfg.control_tls.cert_file;
    tlsCfg.keyFile = cfg.control_tls.key_file;
    tlsCfg.sslDll = cfg.control_tls.ssl_dll;
    tlsCfg.cryptoDll = cfg.control_tls.crypto_dll;
    return tlsCfg;
}

bool ValidateStartupConfig(const RuntimeConfig& cfg, const int port, const TlsServerConfig& tlsCfg, std::string& outErr) {
    if ((port < kMinPort) || (port > kMaxPort)) {
        outErr = "[CFG] invalid --port value: " + std::to_string(port) + " (expected 1..65535)";
        return false;
    }
    if (cfg.server_listen_backlog <= 0) {
        outErr = "[CFG] invalid server_listen_backlog: " + std::to_string(cfg.server_listen_backlog);
        return false;
    }

    if (!tlsCfg.enabled) {
        return true;
    }

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

class RuntimeCleanupGuard {
public:
    RuntimeCleanupGuard(ServerRuntimeContext& runtimeCtx, ServerSocketContext& serverCtx)
        : runtimeCtx_(runtimeCtx), serverCtx_(serverCtx) {}

    ~RuntimeCleanupGuard() {
        ShutdownRuntime(runtimeCtx_);
        ShutdownServerSocket(serverCtx_);
    }

    RuntimeCleanupGuard(const RuntimeCleanupGuard&) = delete;
    RuntimeCleanupGuard& operator=(const RuntimeCleanupGuard&) = delete;

private:
    ServerRuntimeContext& runtimeCtx_;
    ServerSocketContext& serverCtx_;
};
}  // namespace

int main(int argc, char** argv) {
    const RuntimeConfig& cfg = GetRuntimeConfig();
    int port = kDefaultPort;
    std::string argError;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!ParsePortArgument(arg, port, argError)) {
            LogError(argError);
            return 1;
        }
    }

    const TlsServerConfig tlsCfg = BuildTlsServerConfig(cfg);

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
    RuntimeCleanupGuard cleanupGuard(runtimeCtx, serverCtx);

    while (true) {
        JoinFinishedStreamThreads(runtimeCtx);
        sockaddr_in clientAddr{};
        int clientLen = static_cast<int>(sizeof(clientAddr));
        ServerClient client;
        if (!AcceptServerClient(serverCtx, client, &clientAddr, &clientLen)) {
            continue;
        }

        std::array<char, kClientBufferSize> buf{};
        const int len = ClientRecv(client, buf.data(), static_cast<int>(buf.size() - 1U));
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
        buf[static_cast<std::size_t>(len)] = '\0';

        const std::string line(buf.data(), static_cast<std::size_t>(len));
        {
            char ip[64] = {0};
            inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
            LogInfo(std::string("Request from ") + ip + ":" +
                    std::to_string(ntohs(clientAddr.sin_port)) + " -> " + line);
        }
        Request req = ParseRequest(line);
        HandleClientRequest(client, req, runtimeCtx);
    }
}

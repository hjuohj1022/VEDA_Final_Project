#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <thread>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "app_config.h"
#include "command_dispatcher.h"
#include "logging.h"
#include "request.h"
#include "runtime_config.h"
#include "server_bootstrap.h"
#include "server_runtime.h"
#include "service_app.h"

namespace {
constexpr std::size_t kClientBufferSize = 1024U;

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

int RunDepthService(const ServiceRunOptions& options) {
    const RuntimeConfig& cfg = GetRuntimeConfig();
    const TlsServerConfig tlsCfg = BuildTlsServerConfig(cfg, options.disableControlTls);

    std::string validationErr;
    if (!ValidateServerStartupConfig(cfg, options.port, tlsCfg, validationErr)) {
        LogError(validationErr);
        return 1;
    }

    ServerSocketContext serverCtx;
    if (!InitServerSocket(options.port, cfg.server_listen_backlog, options.bindAddress, &tlsCfg, serverCtx)) {
        return 1;
    }

    LogInfo("Listening on port " + std::to_string(options.port) +
            (tlsCfg.enabled ? " (mTLS)" : " (plain TCP)") +
            (options.bindAddress.empty() ? "" : " bind=" + options.bindAddress));

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

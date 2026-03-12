#include <atomic>
#include <string>
#include <thread>

#include "app_config.h"
#include "command_dispatcher.h"
#include "control_server_common.h"
#include "logging.h"
#include "runtime_config.h"
#include "server_bootstrap.h"
#include "server_runtime.h"
#include "service_app.h"

namespace {
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

    ServerSocketContext serverCtx;
    if (!InitControlServerContext(cfg, options.port, options.bindAddress, tlsCfg, "", serverCtx)) {
        return 1;
    }

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
        ParsedControlRequest requestCtx;
        if (!AcceptParsedControlRequest(serverCtx, cfg, requestCtx)) {
            continue;
        }
        HandleClientRequest(requestCtx.client, requestCtx.request, runtimeCtx);
    }
}

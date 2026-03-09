#include <string>
#include <mutex>

#include "app_config.h"
#include "command_dispatcher.h"
#include "logging.h"
#include "net_protocol.h"
#include "request_validator.h"
#include "runner.h"

namespace {
class ClientSocketHandle {
public:
    explicit ClientSocketHandle(ServerClient s) : client_(s) {}
    ~ClientSocketHandle() {
        CloseServerClient(client_);
    }

    const ServerClient& get() const { return client_; }

    ServerClient Release() {
        ServerClient out = client_;
        client_ = ServerClient{};
        return out;
    }

private:
    ServerClient client_{};
};

template <typename StreamBuffer>
void StopStreamThread(std::thread& streamThread, StreamBuffer& streamBuffer) {
    if (!streamThread.joinable()) {
        return;
    }
    {
        std::unique_lock<std::mutex> lock(streamBuffer.mu);
        streamBuffer.stop = true;
    }
    streamBuffer.cv.notify_all();
    streamThread.join();
}

void StopDepthStreamThread(ServerRuntimeContext& ctx) {
    StopStreamThread(ctx.depthStreamThread, ctx.depthStream);
}

void StopRgbdStreamThread(ServerRuntimeContext& ctx) {
    StopStreamThread(ctx.rgbdStreamThread, ctx.rgbdStream);
}

void StopPcStreamThread(ServerRuntimeContext& ctx) {
    StopStreamThread(ctx.pcStreamThread, ctx.pcStream);
}

void StopWorker(ServerRuntimeContext& ctx) {
    if (!ctx.workerRunning) {
        return;
    }
    ctx.workerStop.store(true);
    if (ctx.worker.joinable()) {
        ctx.worker.join();
    }
    ctx.workerRunning = false;
}

void HandlePcViewRequest(const ServerClient& client, const Request& req, ServerRuntimeContext& ctx) {
    float rxNow = 0.0f;
    float ryNow = 0.0f;
    bool fxNow = false;
    bool fyNow = false;
    bool fzNow = false;
    bool wireNow = false;
    bool meshNow = false;

    {
        std::lock_guard<std::mutex> lock(ctx.viewParams.mu);
        if (req.rxSet) ctx.viewParams.rotX = req.rx;
        if (req.rySet) ctx.viewParams.rotY = req.ry;
        if (req.flipXSet) ctx.viewParams.flipX = req.flipX;
        if (req.flipYSet) ctx.viewParams.flipY = req.flipY;
        if (req.flipZSet) ctx.viewParams.flipZ = req.flipZ;
        if (req.wireSet) ctx.viewParams.wire = req.wire;
        if (req.meshSet) ctx.viewParams.mesh = req.mesh;
        rxNow = ctx.viewParams.rotX;
        ryNow = ctx.viewParams.rotY;
        fxNow = ctx.viewParams.flipX;
        fyNow = ctx.viewParams.flipY;
        fzNow = ctx.viewParams.flipZ;
        wireNow = ctx.viewParams.wire;
        meshNow = ctx.viewParams.mesh;
    }

    SendResponse(client,
                 "OK pc_view rx=" + std::to_string(rxNow) +
                     " ry=" + std::to_string(ryNow) +
                     " flipx=" + std::to_string(fxNow ? 1 : 0) +
                     " flipy=" + std::to_string(fyNow ? 1 : 0) +
                     " flipz=" + std::to_string(fzNow ? 1 : 0) +
                     " wire=" + std::to_string(wireNow ? 1 : 0) +
                     " mesh=" + std::to_string(meshNow ? 1 : 0) + "\n");
}

void HandlePauseRequest(const ServerClient& client, const Request& req, ServerRuntimeContext& ctx) {
    bool pausedNow = false;
    {
        std::lock_guard<std::mutex> lock(ctx.viewParams.mu);
        ctx.viewParams.paused = req.pause;
        pausedNow = ctx.viewParams.paused;
    }
    SendResponse(client, std::string("OK pause=") + (pausedNow ? "1\n" : "0\n"));
}
}  // namespace

void JoinFinishedStreamThreads(ServerRuntimeContext& ctx) {
    if (ctx.depthStreamThread.joinable() && !ctx.depthStreamActive.load()) {
        ctx.depthStreamThread.join();
    }
    if (ctx.rgbdStreamThread.joinable() && !ctx.rgbdStreamActive.load()) {
        ctx.rgbdStreamThread.join();
    }
    if (ctx.pcStreamThread.joinable() && !ctx.pcStreamActive.load()) {
        ctx.pcStreamThread.join();
    }
}

void ShutdownRuntime(ServerRuntimeContext& ctx) {
    StopWorker(ctx);
    StopDepthStreamThread(ctx);
    StopRgbdStreamThread(ctx);
    StopPcStreamThread(ctx);
}

void HandleClientRequest(ServerClient client, const Request& req, ServerRuntimeContext& ctx) {
    ClientSocketHandle clientSocket(client);

    const RequestValidationResult valid = ValidateRequest(req);
    if (!valid.ok) {
        LogWarn("Invalid request: " + valid.error);
        SendResponse(clientSocket.get(), valid.error);
        return;
    }

    if (req.depthStream) {
        StopDepthStreamThread(ctx);
        ServerClient streamClient = clientSocket.Release();
        ctx.depthStreamThread = std::thread([streamClient, &ctx]() {
            DepthStreamWorker(streamClient, &ctx.depthStream, &ctx.depthStreamActive);
        });
        return;
    }

    if (req.rgbdStream) {
        StopRgbdStreamThread(ctx);
        ServerClient streamClient = clientSocket.Release();
        ctx.rgbdStreamThread = std::thread([streamClient, &ctx]() {
            RgbdStreamWorker(streamClient, &ctx.rgbdStream, &ctx.rgbdStreamActive);
        });
        return;
    }

    if (req.pcStream) {
        StopPcStreamThread(ctx);
        ServerClient streamClient = clientSocket.Release();
        ctx.pcStreamThread = std::thread([streamClient, &ctx]() {
            PcImageStreamWorker(streamClient, &ctx.pcStream, &ctx.pcStreamActive);
        });
        return;
    }

    if (req.pcView) {
        HandlePcViewRequest(clientSocket.get(), req, ctx);
        return;
    }

    if (req.pauseSet) {
        HandlePauseRequest(clientSocket.get(), req, ctx);
        return;
    }

    StopWorker(ctx);
    if (req.stop) {
        LogInfo("Stop request processed");
        SendResponse(clientSocket.get(), "OK stopped\n");
        return;
    }

    const int channel = (req.channel >= 0) ? req.channel : RTSP_CHANNEL;
    bool headless = req.headlessSet ? req.headless : false;
    if (req.gui) {
        headless = false;
    }

    ctx.workerStop.store(false);
    ctx.worker = std::thread([channel, headless, &ctx]() {
        const bool ok = RunDepthWorker(channel, headless, ctx.workerStop,
                                       &ctx.depthStream, &ctx.rgbdStream, &ctx.pcStream, &ctx.viewParams);
        if (!ok) {
            LogError("Worker exited with errors.");
        }
    });
    ctx.workerRunning = true;

    const std::string modeStr = headless ? "headless" : "gui";
    SendResponse(clientSocket.get(), "OK started channel=" + std::to_string(channel) + " mode=" + modeStr + "\n");
}

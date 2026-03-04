#include <string>
#include <mutex>

#include <winsock2.h>

#include "app_config.h"
#include "command_dispatcher.h"
#include "logging.h"
#include "net_protocol.h"
#include "request_validator.h"
#include "runner.h"

namespace {
class ClientSocketHandle {
public:
    explicit ClientSocketHandle(SOCKET s) : socket_(s) {}
    ~ClientSocketHandle() {
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
        }
    }

    SOCKET get() const { return socket_; }

    SOCKET Release() {
        SOCKET out = socket_;
        socket_ = INVALID_SOCKET;
        return out;
    }

private:
    SOCKET socket_ = INVALID_SOCKET;
};

void StopDepthStreamThread(ServerRuntimeContext& ctx) {
    if (!ctx.depthStreamThread.joinable()) return;
    {
        std::unique_lock<std::mutex> lock(ctx.depthStream.mu);
        ctx.depthStream.stop = true;
    }
    ctx.depthStream.cv.notify_all();
    ctx.depthStreamThread.join();
}

void StopRgbdStreamThread(ServerRuntimeContext& ctx) {
    if (!ctx.rgbdStreamThread.joinable()) return;
    {
        std::unique_lock<std::mutex> lock(ctx.rgbdStream.mu);
        ctx.rgbdStream.stop = true;
    }
    ctx.rgbdStream.cv.notify_all();
    ctx.rgbdStreamThread.join();
}

void StopPcStreamThread(ServerRuntimeContext& ctx) {
    if (!ctx.pcStreamThread.joinable()) return;
    {
        std::unique_lock<std::mutex> lock(ctx.pcStream.mu);
        ctx.pcStream.stop = true;
    }
    ctx.pcStream.cv.notify_all();
    ctx.pcStreamThread.join();
}

void StopWorker(ServerRuntimeContext& ctx) {
    if (!ctx.workerRunning) return;
    ctx.workerStop.store(true);
    if (ctx.worker.joinable()) ctx.worker.join();
    ctx.workerRunning = false;
}

void HandlePcViewRequest(SOCKET client, const Request& req, ServerRuntimeContext& ctx) {
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

void HandlePauseRequest(SOCKET client, const Request& req, ServerRuntimeContext& ctx) {
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

void HandleClientRequest(SOCKET client, const Request& req, ServerRuntimeContext& ctx) {
    ClientSocketHandle clientSocket(client);

    RequestValidationResult valid = ValidateRequest(req);
    if (!valid.ok) {
        LogWarn("Invalid request: " + valid.error);
        SendResponse(clientSocket.get(), valid.error);
        return;
    }

    if (req.depthStream) {
        StopDepthStreamThread(ctx);
        SOCKET streamClient = clientSocket.Release();
        ctx.depthStreamThread = std::thread([streamClient, &ctx]() {
            DepthStreamWorker(streamClient, &ctx.depthStream, &ctx.depthStreamActive);
        });
        return;
    }

    if (req.rgbdStream) {
        StopRgbdStreamThread(ctx);
        SOCKET streamClient = clientSocket.Release();
        ctx.rgbdStreamThread = std::thread([streamClient, &ctx]() {
            RgbdStreamWorker(streamClient, &ctx.rgbdStream, &ctx.rgbdStreamActive);
        });
        return;
    }

    if (req.pcStream) {
        StopPcStreamThread(ctx);
        SOCKET streamClient = clientSocket.Release();
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

    int channel = req.channel >= 0 ? req.channel : RTSP_CHANNEL;
    bool headless = req.headlessSet ? req.headless : false;
    if (req.gui) headless = false;

    ctx.workerStop.store(false);
    ctx.worker = std::thread([channel, headless, &ctx]() {
        bool ok = RunDepthWorker(channel, headless, ctx.workerStop,
                                 &ctx.depthStream, &ctx.rgbdStream, &ctx.pcStream, &ctx.viewParams);
        if (!ok) LogError("Worker exited with errors.");
    });
    ctx.workerRunning = true;

    std::string modeStr = headless ? "headless" : "gui";
    SendResponse(clientSocket.get(), "OK started channel=" + std::to_string(channel) + " mode=" + modeStr + "\n");
}

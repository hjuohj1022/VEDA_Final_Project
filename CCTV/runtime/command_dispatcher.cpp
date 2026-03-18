#include <cstdlib>
#include <chrono>
#include <memory>
#include <system_error>
#include <string>
#include <mutex>

#include "app_config.h"
#include "command_dispatcher.h"
#include "logging.h"
#include "net_protocol.h"
#include "request_validator.h"
#include "runner.h"
#include "runtime_config.h"

namespace {
constexpr int kWorkerStartupSlackMs = 5000;
constexpr int kWorkerStartupFailureExitCode = 210;
constexpr int kWorkerRuntimeFailureExitCode = 211;
constexpr int kWorkerStartupTimeoutExitCode = 212;

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
    SOCKET clientSocket = INVALID_SOCKET;
    {
        std::unique_lock<std::mutex> lock(streamBuffer.mu);
        streamBuffer.stop = true;
        clientSocket = streamBuffer.activeSocket;
    }
    if (clientSocket != INVALID_SOCKET) {
        shutdown(clientSocket, SD_BOTH);
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

bool IsStreamRequest(const Request& req) {
    return req.depthStream || req.rgbdStream || req.pcStream;
}

void ResetPausedState(ServerRuntimeContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.controlState.mu);
    ctx.controlState.paused = false;
}

void StopWorker(ServerRuntimeContext& ctx) {
    if (ctx.workerRunning) {
        ctx.workerStop.store(true);
    }

    StopDepthStreamThread(ctx);
    StopRgbdStreamThread(ctx);
    StopPcStreamThread(ctx);

    if (ctx.workerRunning) {
        if (ctx.worker.joinable()) {
            ctx.worker.join();
        }
        ctx.workerRunning = false;
    }

    ResetPausedState(ctx);
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
        std::lock_guard<std::mutex> lock(ctx.controlState.mu);
        ctx.controlState.paused = req.pause;
        pausedNow = ctx.controlState.paused;
    }
    SendResponse(client, std::string("OK pause=") + (pausedNow ? "1\n" : "0\n"));
}

void HandleStatusRequest(const ServerClient& client, ServerRuntimeContext& ctx) {
    bool pausedNow = false;
    {
        std::lock_guard<std::mutex> lock(ctx.controlState.mu);
        pausedNow = ctx.controlState.paused;
    }
    SendResponse(client,
                 "OK status worker_running=" + std::to_string(ctx.workerRunning ? 1 : 0) +
                     " paused=" + std::to_string(pausedNow ? 1 : 0) + "\n");
}

template <typename StartFn>
void LaunchStreamThreadOrReply(const char* streamName,
                               std::atomic<bool>& streamActive,
                               ServerClient streamClient,
                               StartFn&& startFn) {
    streamActive.store(true);
    try {
        startFn();
    } catch (const std::system_error& ex) {
        streamActive.store(false);
        LogError(std::string(streamName) + " thread launch failed: " + ex.what());
        SendResponse(streamClient, std::string("ERR ") + streamName + " launch failed\n");
        CloseServerClient(streamClient);
    }
}

void ResetWorkerStartupState(WorkerStartupState& state) {
    std::lock_guard<std::mutex> lock(state.mu);
    state.finished = false;
    state.success = false;
    state.detail.clear();
}

[[noreturn]] void ExitWorkerServiceProcess(const int exitCode, const std::string& reason) {
    LogError(reason);
    std::_Exit(exitCode);
}

void SendFinalResponseAndExit(ClientSocketHandle& clientSocket, const std::string& response,
                              const int exitCode, const std::string& reason) {
    ServerClient responseClient = clientSocket.Release();
    SendResponse(responseClient, response);
    CloseServerClient(responseClient);
    ExitWorkerServiceProcess(exitCode, reason);
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
}

void HandleClientRequest(ServerClient client, const Request& req, ServerRuntimeContext& ctx) {
    ClientSocketHandle clientSocket(client);

    const RequestValidationResult valid = ValidateRequest(req);
    if (!valid.ok) {
        LogWarn("Invalid request: " + valid.error);
        SendResponse(clientSocket.get(), valid.error);
        return;
    }

    if (IsStreamRequest(req) && !ctx.workerRunning) {
        SendResponse(clientSocket.get(), "ERR worker not running\n");
        return;
    }

    if (req.depthStream) {
        StopDepthStreamThread(ctx);
        ServerClient streamClient = clientSocket.Release();
        LaunchStreamThreadOrReply("depth_stream", ctx.depthStreamActive, streamClient, [&ctx, streamClient]() {
            ctx.depthStreamThread = std::thread([streamClient, &ctx]() {
                DepthStreamWorker(streamClient, &ctx.depthStream, &ctx.depthStreamActive);
            });
        });
        return;
    }

    if (req.rgbdStream) {
        StopRgbdStreamThread(ctx);
        ServerClient streamClient = clientSocket.Release();
        LaunchStreamThreadOrReply("rgbd_stream", ctx.rgbdStreamActive, streamClient, [&ctx, streamClient]() {
            ctx.rgbdStreamThread = std::thread([streamClient, &ctx]() {
                RgbdStreamWorker(streamClient, &ctx.rgbdStream, &ctx.rgbdStreamActive);
            });
        });
        return;
    }

    if (req.pcStream) {
        StopPcStreamThread(ctx);
        ServerClient streamClient = clientSocket.Release();
        LaunchStreamThreadOrReply("pc_stream", ctx.pcStreamActive, streamClient, [&ctx, streamClient]() {
            ctx.pcStreamThread = std::thread([streamClient, &ctx]() {
                PcImageStreamWorker(streamClient, &ctx.pcStream, &ctx.pcStreamActive);
            });
        });
        return;
    }

    if (req.pcView) {
        HandlePcViewRequest(clientSocket.get(), req, ctx);
        return;
    }

    if (req.statusQuery) {
        HandleStatusRequest(clientSocket.get(), ctx);
        return;
    }

    if (req.pauseSet) {
        if (!ctx.workerRunning) {
            SendResponse(clientSocket.get(), "ERR worker not running\n");
            return;
        }
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
    auto startupState = std::make_shared<WorkerStartupState>();
    ResetWorkerStartupState(*startupState);
    ctx.worker = std::thread([channel, headless, &ctx, startupState]() {
        const bool ok = RunDepthWorker(channel, headless, ctx.workerStop,
                                       &ctx.depthStream, &ctx.rgbdStream, &ctx.pcStream,
                                       &ctx.viewParams, &ctx.controlState,
                                       startupState.get());
        if (!ok) {
            LogError("Worker exited with errors.");
            bool startupSucceeded = false;
            {
                std::lock_guard<std::mutex> lock(startupState->mu);
                startupSucceeded = startupState->finished && startupState->success;
            }
            if (startupSucceeded) {
                ExitWorkerServiceProcess(kWorkerRuntimeFailureExitCode,
                                         "Fatal worker runtime failure. Terminating worker service process.");
            }
        }
    });
    ctx.workerRunning = true;

    const RuntimeConfig& cfg = GetRuntimeConfig();
    const auto startupTimeout = std::chrono::milliseconds(
        cfg.open_timeout_ms + cfg.read_timeout_ms + kWorkerStartupSlackMs);
    std::unique_lock<std::mutex> startupLock(startupState->mu);
    const bool startupFinished = startupState->cv.wait_for(startupLock, startupTimeout, [&startupState]() {
        return startupState->finished;
    });
    if (!startupFinished) {
        startupLock.unlock();
        SendFinalResponseAndExit(clientSocket,
                                 "ERR worker start timeout\n",
                                 kWorkerStartupTimeoutExitCode,
                                 "Worker startup timed out before ready signal. Recycling worker process.");
    }
    const bool startupOk = startupState->success;
    const std::string startupDetail = startupState->detail;
    startupLock.unlock();
    if (!startupOk) {
        SendFinalResponseAndExit(clientSocket,
                                 "ERR worker start failed: " + startupDetail + "\n",
                                 kWorkerStartupFailureExitCode,
                                 "Worker startup failed. Recycling worker process. detail=" + startupDetail);
    }

    const std::string modeStr = headless ? "headless" : "gui";
    SendResponse(clientSocket.get(), "OK started channel=" + std::to_string(channel) + " mode=" + modeStr + "\n");
}

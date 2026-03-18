#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

#include <winsock2.h>

#include "control_server_common.h"
#include "logging.h"
#include "net_protocol.h"
#include "proxy_server.h"
#include "runtime_config.h"
#include "server_bootstrap.h"
#include "server_runtime.h"
#include "worker_process.h"

namespace {
class ClientSocketHandle {
public:
    explicit ClientSocketHandle(ServerClient client) : client_(client) {}
    ~ClientSocketHandle() {
        CloseServerClient(client_);
    }

    const ServerClient& get() const { return client_; }

private:
    ServerClient client_{};
};

class RawSocketHandle {
public:
    explicit RawSocketHandle(const SOCKET socket) : socket_(socket) {}
    ~RawSocketHandle() {
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
        }
    }

    SOCKET get() const { return socket_; }

private:
    SOCKET socket_ = INVALID_SOCKET;
};

class WorkerShutdownGuard {
public:
    WorkerShutdownGuard(WorkerProcessManager* workerMgr, std::atomic<bool>* workerStopping, const bool enabled)
        : workerMgr_(workerMgr), workerStopping_(workerStopping), enabled_(enabled) {}

    ~WorkerShutdownGuard() {
        if (!enabled_ || !workerMgr_) {
            return;
        }
        workerMgr_->Shutdown();
        if (workerStopping_) {
            workerStopping_->store(false);
        }
        LogInfo("[PROXY] worker process shut down after stop request");
    }

    WorkerShutdownGuard(const WorkerShutdownGuard&) = delete;
    WorkerShutdownGuard& operator=(const WorkerShutdownGuard&) = delete;

private:
    WorkerProcessManager* workerMgr_ = nullptr;
    std::atomic<bool>* workerStopping_ = nullptr;
    bool enabled_ = false;
};

class ProxyCleanupGuard {
public:
    ProxyCleanupGuard(ServerSocketContext& serverCtx, WorkerProcessManager& workerMgr)
        : serverCtx_(serverCtx), workerMgr_(workerMgr) {}

    ~ProxyCleanupGuard() {
        workerMgr_.Shutdown();
        ShutdownServerSocket(serverCtx_);
    }

    ProxyCleanupGuard(const ProxyCleanupGuard&) = delete;
    ProxyCleanupGuard& operator=(const ProxyCleanupGuard&) = delete;

private:
    ServerSocketContext& serverCtx_;
    WorkerProcessManager& workerMgr_;
};

class ActiveProxyClientGuard {
public:
    explicit ActiveProxyClientGuard(std::atomic<int>& activeClients) : activeClients_(activeClients) {}
    ~ActiveProxyClientGuard() {
        activeClients_.fetch_sub(1);
    }

    ActiveProxyClientGuard(const ActiveProxyClientGuard&) = delete;
    ActiveProxyClientGuard& operator=(const ActiveProxyClientGuard&) = delete;

private:
    std::atomic<int>& activeClients_;
};

bool SendAllToClient(const ServerClient& client, const char* data, const int bytes) {
    int offset = 0;
    while (offset < bytes) {
        const int sent = ClientSend(client, data + offset, bytes - offset);
        if (sent <= 0) {
            return false;
        }
        offset += sent;
    }
    return true;
}

bool TryAcquireProxyClientSlot(std::atomic<int>& activeClients, const int maxClients, int& outActiveClients) {
    int current = activeClients.load();
    while (true) {
        if (current >= maxClients) {
            return false;
        }
        if (activeClients.compare_exchange_weak(current, current + 1)) {
            outActiveClients = current + 1;
            return true;
        }
    }
}

bool ApplySocketIoTimeouts(const SOCKET socket, const int timeoutMs) {
    if (socket == INVALID_SOCKET || timeoutMs <= 0) {
        return true;
    }

    const DWORD timeout = static_cast<DWORD>(timeoutMs);
    const bool recvOk = setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                                   reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
    const bool sendOk = setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                                   reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
    return recvOk && sendOk;
}

bool ConnectWorkerWithRetry(WorkerProcessManager& workerMgr, SOCKET& outSocket, std::string& outErr) {
    if (!workerMgr.EnsureRunning(outErr)) {
        return false;
    }

    if (ConnectToWorker(workerMgr.port(), outSocket, outErr)) {
        return true;
    }

    if (!workerMgr.EnsureRunning(outErr)) {
        return false;
    }
    return ConnectToWorker(workerMgr.port(), outSocket, outErr, 2000);
}

bool IsStreamRequest(const Request& req) {
    return req.depthStream || req.rgbdStream || req.pcStream;
}

bool RequiresRunningWorker(const Request& req) {
    return IsStreamRequest(req);
}

bool IsStartRequest(const Request& req) {
    if (req.statusQuery || req.stop || req.pauseSet ||
        req.depthStream || req.rgbdStream || req.pcStream || req.pcView) {
        return false;
    }
    return (req.channel >= 0) || req.headlessSet || req.gui;
}

bool ReceiveTextResponse(const SOCKET socket, std::string& outResponse, std::string& outErr) {
    outResponse.clear();
    std::array<char, 256> responseBuf{};
    while (outResponse.find('\n') == std::string::npos && outResponse.size() < responseBuf.size()) {
        const int received = recv(socket, responseBuf.data(), static_cast<int>(responseBuf.size()), 0);
        if (received == 0) {
            outErr = "worker status recv closed";
            return false;
        }
        if (received < 0) {
            outErr = "worker status recv failed (wsa=" + std::to_string(WSAGetLastError()) + ")";
            return false;
        }
        outResponse.append(responseBuf.data(), static_cast<std::size_t>(received));
    }
    if (outResponse.empty()) {
        outErr = "worker status response empty";
        return false;
    }
    return true;
}

bool QueryWorkerReady(WorkerProcessManager& workerMgr, const int timeoutMs, bool& outReady) {
    outReady = false;
    if (!workerMgr.IsRunning()) {
        return false;
    }

    std::string err;
    SOCKET workerSocket = INVALID_SOCKET;
    if (!ConnectToWorker(workerMgr.port(), workerSocket, err, static_cast<DWORD>(timeoutMs))) {
        return false;
    }
    RawSocketHandle workerHandle(workerSocket);

    if (!ApplySocketIoTimeouts(workerHandle.get(), timeoutMs)) {
        LogWarn("[PROXY] failed to apply worker status timeout (wsa=" + std::to_string(WSAGetLastError()) + ")");
    }

    const std::string statusRequest = "status";
    if (!SendAllToSocket(workerHandle.get(), statusRequest.data(), static_cast<int>(statusRequest.size()), err)) {
        return false;
    }

    std::string response;
    if (!ReceiveTextResponse(workerHandle.get(), response, err)) {
        return false;
    }

    outReady = response.find("worker_running=1") != std::string::npos;
    return true;
}

enum class WorkerReadyWaitResult {
    kReady,
    kTimeout,
    kStopping,
};

void ExecuteAsyncStop(WorkerProcessManager* workerMgr,
                      std::atomic<bool>* workerStopping,
                      const int relayTimeoutMs) {
    if (!workerMgr) {
        if (workerStopping) {
            workerStopping->store(false);
        }
        return;
    }

    WorkerShutdownGuard workerShutdownGuard(workerMgr, workerStopping, true);
    const int stopRelayTimeoutMs =
        (relayTimeoutMs > 0 && relayTimeoutMs < 2000) ? relayTimeoutMs : 2000;

    std::string stopErr;
    SOCKET workerSocket = INVALID_SOCKET;
    if (!ConnectToWorker(workerMgr->port(), workerSocket, stopErr, 500)) {
        LogWarn("[PROXY] async worker stop connect failed: " + stopErr);
        return;
    }

    RawSocketHandle workerHandle(workerSocket);
    if (!ApplySocketIoTimeouts(workerHandle.get(), stopRelayTimeoutMs)) {
        LogWarn("[PROXY] failed to apply async worker stop timeout (wsa=" +
                std::to_string(WSAGetLastError()) + ")");
    }

    const std::string stopRequest = "stop";
    if (!SendAllToSocket(workerHandle.get(), stopRequest.data(), static_cast<int>(stopRequest.size()), stopErr)) {
        LogWarn("[PROXY] failed to forward async stop to worker: " + stopErr);
        return;
    }

    std::array<char, 4096> responseBuf{};
    const int received = recv(workerHandle.get(), responseBuf.data(),
                              static_cast<int>(responseBuf.size()), 0);
    if (received == 0) {
        return;
    }
    if (received < 0) {
        const int wsaErr = WSAGetLastError();
        if (wsaErr != WSAETIMEDOUT) {
            LogWarn("[PROXY] async worker stop recv failed (wsa=" + std::to_string(wsaErr) + ")");
        }
    }
}

WorkerReadyWaitResult WaitForWorkerReady(WorkerProcessManager& workerMgr,
                                         std::atomic<bool>* workerStopping,
                                         const RuntimeConfig& cfg) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(cfg.worker_ready_wait_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (workerStopping && workerStopping->load()) {
            return WorkerReadyWaitResult::kStopping;
        }

        const auto remaining = deadline - std::chrono::steady_clock::now();
        const int remainingMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
        if (remainingMs <= 0) {
            break;
        }

        bool ready = false;
        if (QueryWorkerReady(workerMgr, remainingMs, ready) && ready) {
            return WorkerReadyWaitResult::kReady;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return WorkerReadyWaitResult::kTimeout;
}

void HandleProxyClient(ParsedControlRequest requestCtx,
                       WorkerProcessManager* workerMgr,
                       std::atomic<int>* activeClients,
                       std::atomic<bool>* workerStopping) {
    ClientSocketHandle clientHandle(requestCtx.client);
    ActiveProxyClientGuard activeGuard(*activeClients);
    const Request& req = requestCtx.request;
    const std::string& line = requestCtx.line;
    const int requestLen = static_cast<int>(line.size());
    const RuntimeConfig& cfg = GetRuntimeConfig();
    if (workerStopping && workerStopping->load()) {
        if (req.stop) {
            SendResponse(clientHandle.get(), "OK stopped\n");
        } else {
            SendResponse(clientHandle.get(), "ERR worker stopping\n");
        }
        return;
    }

    const bool workerServiceRunning = workerMgr->IsRunning();
    const bool startRequest = IsStartRequest(req);
    if (req.stop && !workerServiceRunning) {
        SendResponse(clientHandle.get(), "OK stopped\n");
        return;
    }
    if (RequiresRunningWorker(req)) {
        const WorkerReadyWaitResult waitResult = WaitForWorkerReady(*workerMgr, workerStopping, cfg);
        if (waitResult == WorkerReadyWaitResult::kStopping) {
            SendResponse(clientHandle.get(), "ERR worker stopping\n");
            return;
        }
        if (waitResult == WorkerReadyWaitResult::kTimeout) {
            SendResponse(clientHandle.get(), "ERR worker not running\n");
            return;
        }
    } else if (!workerServiceRunning && !startRequest) {
        SendResponse(clientHandle.get(), "ERR worker not running\n");
        return;
    }

    bool stopGateEnabled = false;
    if (req.stop && workerStopping) {
        bool expected = false;
        if (!workerStopping->compare_exchange_strong(expected, true)) {
            SendResponse(clientHandle.get(), "OK stopped\n");
            return;
        }
        stopGateEnabled = true;
    }

    if (req.stop) {
        SendResponse(clientHandle.get(), "OK stopped\n");
        if (stopGateEnabled) {
            LogInfo("[PROXY] stop request accepted; worker teardown running in background");
            std::thread(ExecuteAsyncStop, workerMgr, workerStopping, cfg.proxy_relay_io_timeout_ms).detach();
        }
        return;
    }

    std::string err;
    SOCKET workerSocket = INVALID_SOCKET;
    if (startRequest) {
        if (!ConnectWorkerWithRetry(*workerMgr, workerSocket, err)) {
            LogError("[PROXY] worker unavailable: " + err);
            SendResponse(clientHandle.get(), "ERR worker unavailable\n");
            return;
        }
    } else {
        if (!workerMgr->IsRunning()) {
            err = "worker service not running";
        } else if (ConnectToWorker(workerMgr->port(), workerSocket, err, 2000)) {
            err.clear();
        }
        if (!err.empty()) {
            LogWarn("[PROXY] worker unavailable without start: " + err);
            SendResponse(clientHandle.get(), "ERR worker not running\n");
            return;
        }
    }
    RawSocketHandle workerHandle(workerSocket);

    if (!IsStreamRequest(req)) {
        if (!ApplySocketIoTimeouts(workerHandle.get(), cfg.proxy_relay_io_timeout_ms)) {
            LogWarn("[PROXY] failed to apply worker relay timeout (wsa=" + std::to_string(WSAGetLastError()) + ")");
        }
        if (!ApplySocketIoTimeouts(clientHandle.get().socket, cfg.proxy_relay_io_timeout_ms)) {
            LogWarn("[PROXY] failed to apply client relay timeout (wsa=" + std::to_string(WSAGetLastError()) + ")");
        }
    }

    if (!SendAllToSocket(workerHandle.get(), line.data(), requestLen, err)) {
        LogWarn("[PROXY] failed to forward request to worker: " + err);
        SendResponse(clientHandle.get(), "ERR worker forward failed\n");
        return;
    }

    std::array<char, 4096> responseBuf{};
    while (true) {
        const int received = recv(workerHandle.get(), responseBuf.data(),
                                  static_cast<int>(responseBuf.size()), 0);
        if (received == 0) {
            return;
        }
        if (received < 0) {
            LogWarn("[PROXY] worker recv failed (wsa=" + std::to_string(WSAGetLastError()) + ")");
            return;
        }
        if (!SendAllToClient(clientHandle.get(), responseBuf.data(), received)) {
            return;
        }
    }
}
}  // namespace

int RunProxyServer(const ProxyRunOptions& options) {
    const RuntimeConfig& cfg = GetRuntimeConfig();
    const TlsServerConfig tlsCfg = BuildTlsServerConfig(cfg, options.disableControlTls);

    ServerSocketContext serverCtx;
    if (!InitControlServerContext(cfg, options.port, options.bindAddress, tlsCfg,
                                  " | worker=127.0.0.1:" + std::to_string(options.workerPort),
                                  serverCtx)) {
        return 1;
    }

    WorkerProcessManager workerMgr(options.workerPort);
    ProxyCleanupGuard cleanupGuard(serverCtx, workerMgr);
    std::atomic<int> activeClients{0};
    std::atomic<bool> workerStopping{false};

    while (true) {
        ParsedControlRequest requestCtx;
        if (!AcceptParsedControlRequest(serverCtx, cfg, requestCtx, "[PROXY] ")) {
            continue;
        }

        int activeClientCount = 0;
        if (!TryAcquireProxyClientSlot(activeClients, cfg.proxy_max_concurrent_clients, activeClientCount)) {
            LogWarn("[PROXY] rejected request: too many active clients (limit=" +
                    std::to_string(cfg.proxy_max_concurrent_clients) + ")");
            SendResponse(requestCtx.client, "ERR proxy busy\n");
            CloseServerClient(requestCtx.client);
            continue;
        }

        std::thread(HandleProxyClient, std::move(requestCtx), &workerMgr, &activeClients, &workerStopping).detach();
    }
}

#include <array>
#include <atomic>
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

void HandleProxyClient(ParsedControlRequest requestCtx,
                       WorkerProcessManager* workerMgr,
                       std::atomic<int>* activeClients) {
    ClientSocketHandle clientHandle(requestCtx.client);
    ActiveProxyClientGuard activeGuard(*activeClients);
    const Request& req = requestCtx.request;
    const std::string& line = requestCtx.line;
    const int requestLen = static_cast<int>(line.size());
    const RuntimeConfig& cfg = GetRuntimeConfig();
    if (req.stop && !workerMgr->IsRunning()) {
        SendResponse(clientHandle.get(), "OK stopped\n");
        return;
    }

    std::string err;
    SOCKET workerSocket = INVALID_SOCKET;
    if (!ConnectWorkerWithRetry(*workerMgr, workerSocket, err)) {
        LogError("[PROXY] worker unavailable: " + err);
        SendResponse(clientHandle.get(), "ERR worker unavailable\n");
        return;
    }
    RawSocketHandle workerHandle(workerSocket);

    if (!ApplySocketIoTimeouts(workerHandle.get(), cfg.proxy_relay_io_timeout_ms)) {
        LogWarn("[PROXY] failed to apply worker relay timeout (wsa=" + std::to_string(WSAGetLastError()) + ")");
    }
    if (!ApplySocketIoTimeouts(clientHandle.get().socket, cfg.proxy_relay_io_timeout_ms)) {
        LogWarn("[PROXY] failed to apply client relay timeout (wsa=" + std::to_string(WSAGetLastError()) + ")");
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

        std::thread(HandleProxyClient, std::move(requestCtx), &workerMgr, &activeClients).detach();
    }
}

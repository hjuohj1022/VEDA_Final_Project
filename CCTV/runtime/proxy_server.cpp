#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <thread>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "logging.h"
#include "net_protocol.h"
#include "proxy_server.h"
#include "request.h"
#include "runtime_config.h"
#include "server_bootstrap.h"
#include "server_runtime.h"
#include "worker_process.h"

namespace {
constexpr std::size_t kClientBufferSize = 1024U;

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

void LogClientDisconnectBeforeRequest() {
    const ClientIoErrorInfo ioErr = GetLastClientIoError();
    if (ioErr.kind == ClientIoErrorKind::kClosed) {
        LogWarn("[PROXY] client disconnected before request");
    } else {
        LogWarn("[PROXY] client recv failed: " + ioErr.detail);
    }
}

void LogRequest(const sockaddr_in& clientAddr, const std::string& line) {
    char ip[64] = {0};
    inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
    LogInfo(std::string("Request from ") + ip + ":" +
            std::to_string(ntohs(clientAddr.sin_port)) + " -> " + line);
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

void HandleProxyClient(ServerClient client, const sockaddr_in clientAddr, WorkerProcessManager* workerMgr) {
    ClientSocketHandle clientHandle(client);

    std::array<char, kClientBufferSize> requestBuf{};
    const int requestLen = ClientRecv(clientHandle.get(), requestBuf.data(),
                                      static_cast<int>(requestBuf.size() - 1U));
    if (requestLen <= 0) {
        LogClientDisconnectBeforeRequest();
        return;
    }
    requestBuf[static_cast<std::size_t>(requestLen)] = '\0';

    const std::string line(requestBuf.data(), static_cast<std::size_t>(requestLen));
    LogRequest(clientAddr, line);

    const Request req = ParseRequest(line);
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

    std::string validationErr;
    if (!ValidateServerStartupConfig(cfg, options.port, tlsCfg, validationErr)) {
        LogError(validationErr);
        return 1;
    }

    ServerSocketContext serverCtx;
    if (!InitServerSocket(options.port, cfg.server_listen_backlog, options.bindAddress, &tlsCfg, serverCtx)) {
        return 1;
    }

    WorkerProcessManager workerMgr(options.workerPort);
    ProxyCleanupGuard cleanupGuard(serverCtx, workerMgr);

    LogInfo("Listening on port " + std::to_string(options.port) +
            (tlsCfg.enabled ? " (mTLS)" : " (plain TCP)") +
            (options.bindAddress.empty() ? "" : " bind=" + options.bindAddress) +
            " | worker=127.0.0.1:" + std::to_string(options.workerPort));

    while (true) {
        sockaddr_in clientAddr{};
        int clientLen = static_cast<int>(sizeof(clientAddr));
        ServerClient client;
        if (!AcceptServerClient(serverCtx, client, &clientAddr, &clientLen)) {
            continue;
        }

        const ServerClient threadClient = client;
        client = ServerClient{};
        std::thread(HandleProxyClient, threadClient, clientAddr, &workerMgr).detach();
    }
}

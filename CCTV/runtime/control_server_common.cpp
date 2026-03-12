#include "control_server_common.h"

#include <array>
#include <cstddef>
#include <string>

#include <ws2tcpip.h>

#include "logging.h"
#include "server_bootstrap.h"

namespace {
constexpr std::size_t kClientBufferSize = 1024U;

bool ApplySocketRecvTimeout(const SOCKET socket, const int timeoutMs) {
    if (socket == INVALID_SOCKET || timeoutMs <= 0) {
        return true;
    }

    const DWORD timeout = static_cast<DWORD>(timeoutMs);
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
}

void LogClientReceiveFailure(const std::string& prefix) {
    const ClientIoErrorInfo ioErr = GetLastClientIoError();
    if (ioErr.kind == ClientIoErrorKind::kClosed) {
        LogWarn(prefix + "client disconnected before request");
    } else {
        LogWarn(prefix + "client recv failed: " + ioErr.detail);
    }
}

void LogAcceptedRequest(const sockaddr_in& clientAddr, const std::string& line) {
    char ip[64] = {0};
    inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
    LogInfo(std::string("Request from ") + ip + ":" +
            std::to_string(ntohs(clientAddr.sin_port)) + " -> " + line);
}

void LogListeningState(const int port,
                       const TlsServerConfig& tlsCfg,
                       const std::string& bindAddress,
                       const std::string& listenSuffix) {
    LogInfo("Listening on port " + std::to_string(port) +
            (tlsCfg.enabled ? " (mTLS)" : " (plain TCP)") +
            (bindAddress.empty() ? "" : " bind=" + bindAddress) +
            listenSuffix);
}
}  // namespace

bool InitControlServerContext(const RuntimeConfig& cfg,
                              const int port,
                              const std::string& bindAddress,
                              const TlsServerConfig& tlsCfg,
                              const std::string& listenSuffix,
                              ServerSocketContext& serverCtx) {
    std::string validationErr;
    if (!ValidateServerStartupConfig(cfg, port, tlsCfg, validationErr)) {
        LogError(validationErr);
        return false;
    }

    if (!InitServerSocket(port, cfg.server_listen_backlog, bindAddress, &tlsCfg, serverCtx)) {
        return false;
    }

    LogListeningState(port, tlsCfg, bindAddress, listenSuffix);
    return true;
}

bool AcceptParsedControlRequest(ServerSocketContext& serverCtx,
                                const RuntimeConfig& cfg,
                                ParsedControlRequest& outRequest,
                                const std::string& recvFailurePrefix) {
    sockaddr_in clientAddr{};
    int clientLen = static_cast<int>(sizeof(clientAddr));
    ServerClient client;
    if (!AcceptServerClient(serverCtx, client, &clientAddr, &clientLen)) {
        return false;
    }

    if (!ApplySocketRecvTimeout(client.socket, cfg.control_client_read_timeout_ms)) {
        LogWarn(recvFailurePrefix + "failed to apply client read timeout (wsa=" +
                std::to_string(WSAGetLastError()) + ")");
    }

    std::array<char, kClientBufferSize> requestBuf{};
    const int requestLen = ClientRecv(client, requestBuf.data(),
                                      static_cast<int>(requestBuf.size() - 1U));
    if (requestLen <= 0) {
        LogClientReceiveFailure(recvFailurePrefix);
        CloseServerClient(client);
        return false;
    }
    requestBuf[static_cast<std::size_t>(requestLen)] = '\0';

    outRequest.client = client;
    outRequest.clientAddr = clientAddr;
    outRequest.line.assign(requestBuf.data(), static_cast<std::size_t>(requestLen));
    outRequest.request = ParseRequest(outRequest.line);
    LogAcceptedRequest(outRequest.clientAddr, outRequest.line);
    return true;
}

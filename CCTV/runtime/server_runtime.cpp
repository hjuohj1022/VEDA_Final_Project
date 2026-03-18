#include <string>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "logging.h"
#include "server_runtime.h"
#include "tls_server.h"

namespace {
constexpr WORD kWsaVersion = MAKEWORD(2, 2);

thread_local ClientIoErrorInfo gLastClientIoError;

void SetIoError(ClientIoErrorKind kind, int code, const std::string& detail) {
    gLastClientIoError.kind = kind;
    gLastClientIoError.code = code;
    gLastClientIoError.detail = detail;
}

void SetNoIoError() {
    SetIoError(ClientIoErrorKind::kNone, 0, "");
}

void SetClosedIoError(const std::string& detail) {
    SetIoError(ClientIoErrorKind::kClosed, 0, detail);
}

void SetTlsIoErrorFromLastError() {
    const std::string tlsErr = TlsServerGetLastIoError();
    if (tlsErr.empty() || tlsErr == "no openssl error") {
        SetClosedIoError("tls peer closed");
    } else {
        SetIoError(ClientIoErrorKind::kTlsError, 0, tlsErr);
    }
}

void SetTcpIoErrorFromWsaCode(const int wsaErr, const std::string& operation) {
    if (wsaErr == WSAEWOULDBLOCK) {
        SetIoError(ClientIoErrorKind::kWouldBlock, wsaErr, "tcp " + operation + " would block");
    } else {
        SetIoError(ClientIoErrorKind::kNetworkError, wsaErr,
                   "tcp " + operation + " failed (wsa=" + std::to_string(wsaErr) + ")");
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

bool FailInit(ServerSocketContext& ctx, const std::string& err) {
    if (!err.empty()) {
        LogError("[NET] " + err);
    }
    ShutdownServerSocket(ctx);
    return false;
}
}  // namespace

bool InitServerSocket(int port, int backlog, const std::string& bindAddress,
                      const TlsServerConfig* tlsCfg, ServerSocketContext& ctx) {
    WSADATA wsa{};
    if (WSAStartup(kWsaVersion, &wsa) != 0) {
        return FailInit(ctx, "wsa_startup failed");
    }
    ctx.wsaStarted = true;

    if (tlsCfg && tlsCfg->enabled) {
        std::string err;
        if (!TlsServerInit(*tlsCfg, &ctx.tlsCtx, err)) {
            return FailInit(ctx, err);
        }
        ctx.secureEnabled = true;
        LogInfo("[TLS] control mTLS enabled");
    }

    ctx.server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctx.server == INVALID_SOCKET) {
        return FailInit(ctx, "socket failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    if (bindAddress.empty()) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1) {
        return FailInit(ctx, "invalid bind address: " + bindAddress);
    }
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(ctx.server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        return FailInit(ctx, "bind failed (addr=" + (bindAddress.empty() ? std::string("0.0.0.0") : bindAddress) +
                                 ", port=" + std::to_string(port) +
                                 ", wsa=" + std::to_string(WSAGetLastError()) + ")");
    }

    if (listen(ctx.server, backlog) == SOCKET_ERROR) {
        return FailInit(ctx, "listen failed");
    }

    return true;
}

bool AcceptServerClient(ServerSocketContext& ctx, ServerClient& client, sockaddr_in* clientAddr, int* clientLen) {
    client = ServerClient{};
    sockaddr_in localAddr{};
    int localLen = static_cast<int>(sizeof(localAddr));
    sockaddr_in* outAddr = clientAddr ? clientAddr : &localAddr;
    int* outLen = clientLen ? clientLen : &localLen;

    const SOCKET acceptedSocket = accept(ctx.server, reinterpret_cast<sockaddr*>(outAddr), outLen);
    if (acceptedSocket == INVALID_SOCKET) {
        return false;
    }

    client.socket = acceptedSocket;
    if (!ctx.secureEnabled) {
        return true;
    }

    if (!ApplySocketIoTimeouts(acceptedSocket, ctx.acceptedClientTimeoutMs)) {
        LogWarn("[TLS] failed to apply accepted client handshake timeout (wsa=" +
                std::to_string(WSAGetLastError()) + ")");
        closesocket(acceptedSocket);
        client.socket = INVALID_SOCKET;
        return false;
    }

    std::string err;
    if (!TlsServerAccept(ctx.tlsCtx, static_cast<int>(acceptedSocket), &client.ssl, err)) {
        LogWarn(err);
        closesocket(acceptedSocket);
        client.socket = INVALID_SOCKET;
        return false;
    }
    client.secure = true;
    return true;
}

int ClientRecv(const ServerClient& client, char* buf, int len) {
    if (client.secure) {
        const int n = TlsServerRecv(client.ssl, buf, len);
        if (n > 0) {
            SetNoIoError();
        } else if (n == 0) {
            SetClosedIoError("tls peer closed");
        } else {
            SetTlsIoErrorFromLastError();
        }
        return n;
    }

    const int n = recv(client.socket, buf, len, 0);
    if (n > 0) {
        SetNoIoError();
    } else if (n == 0) {
        SetClosedIoError("tcp peer closed");
    } else {
        const int wsaErr = WSAGetLastError();
        SetTcpIoErrorFromWsaCode(wsaErr, "recv");
    }
    return n;
}

int ClientSend(const ServerClient& client, const char* data, int len) {
    if (client.secure) {
        const int n = TlsServerSend(client.ssl, data, len);
        if (n > 0) {
            SetNoIoError();
        } else if (n == 0) {
            SetClosedIoError("tls peer closed");
        } else {
            SetTlsIoErrorFromLastError();
        }
        return n;
    }

    const int n = send(client.socket, data, len, 0);
    if (n > 0) {
        SetNoIoError();
    } else if (n == 0) {
        SetClosedIoError("tcp peer closed");
    } else {
        const int wsaErr = WSAGetLastError();
        SetTcpIoErrorFromWsaCode(wsaErr, "send");
    }
    return n;
}

ClientIoErrorInfo GetLastClientIoError() {
    return gLastClientIoError;
}

void CloseServerClient(ServerClient& client) {
    if (client.secure && client.ssl) {
        TlsServerCloseClient(client.ssl);
    }
    client.ssl = nullptr;
    if (client.socket != INVALID_SOCKET) {
        closesocket(client.socket);
        client.socket = INVALID_SOCKET;
    }
    client.secure = false;
}

void ShutdownServerSocket(ServerSocketContext& ctx) {
    if (ctx.server != INVALID_SOCKET) {
        closesocket(ctx.server);
        ctx.server = INVALID_SOCKET;
    }
    if (ctx.tlsCtx) {
        TlsServerShutdown(ctx.tlsCtx);
        ctx.tlsCtx = nullptr;
    }
    if (ctx.wsaStarted) {
        WSACleanup();
        ctx.wsaStarted = false;
    }
}

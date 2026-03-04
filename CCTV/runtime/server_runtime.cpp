#include <iostream>
#include <string>

#include <winsock2.h>

#include "logging.h"
#include "server_runtime.h"
#include "tls_server.h"

namespace {
thread_local ClientIoErrorInfo gLastClientIoError;

void SetIoError(ClientIoErrorKind kind, int code, const std::string& detail) {
    gLastClientIoError.kind = kind;
    gLastClientIoError.code = code;
    gLastClientIoError.detail = detail;
}

bool FailInit(ServerSocketContext& ctx, const std::string& err) {
    if (!err.empty()) LogError("[NET] " + err);
    ShutdownServerSocket(ctx);
    return false;
}
}  // namespace

bool InitServerSocket(int port, int backlog, const TlsServerConfig* tlsCfg, ServerSocketContext& ctx) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
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
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(ctx.server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        return FailInit(ctx, "bind failed (port=" + std::to_string(port) +
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
    int localLen = sizeof(localAddr);
    sockaddr_in* outAddr = clientAddr ? clientAddr : &localAddr;
    int* outLen = clientLen ? clientLen : &localLen;

    SOCKET s = accept(ctx.server, reinterpret_cast<sockaddr*>(outAddr), outLen);
    if (s == INVALID_SOCKET) return false;

    client.socket = s;
    if (!ctx.secureEnabled) return true;

    std::string err;
    if (!TlsServerAccept(ctx.tlsCtx, static_cast<int>(s), &client.ssl, err)) {
        LogWarn(err);
        closesocket(s);
        client.socket = INVALID_SOCKET;
        return false;
    }
    client.secure = true;
    return true;
}

int ClientRecv(const ServerClient& client, char* buf, int len) {
    if (client.secure) {
        int n = TlsServerRecv(client.ssl, buf, len);
        if (n > 0) {
            SetIoError(ClientIoErrorKind::kNone, 0, "");
        } else if (n == 0) {
            SetIoError(ClientIoErrorKind::kClosed, 0, "tls peer closed");
        } else {
            const std::string tlsErr = TlsServerGetLastIoError();
            if (tlsErr.empty() || tlsErr == "no openssl error") {
                SetIoError(ClientIoErrorKind::kClosed, 0, "tls peer closed");
            } else {
                SetIoError(ClientIoErrorKind::kTlsError, 0, tlsErr);
            }
        }
        return n;
    }

    int n = recv(client.socket, buf, len, 0);
    if (n > 0) {
        SetIoError(ClientIoErrorKind::kNone, 0, "");
    } else if (n == 0) {
        SetIoError(ClientIoErrorKind::kClosed, 0, "tcp peer closed");
    } else {
        const int wsaErr = WSAGetLastError();
        if (wsaErr == WSAEWOULDBLOCK) {
            SetIoError(ClientIoErrorKind::kWouldBlock, wsaErr, "tcp recv would block");
        } else {
            SetIoError(ClientIoErrorKind::kNetworkError, wsaErr,
                       "tcp recv failed (wsa=" + std::to_string(wsaErr) + ")");
        }
    }
    return n;
}

int ClientSend(const ServerClient& client, const char* data, int len) {
    if (client.secure) {
        int n = TlsServerSend(client.ssl, data, len);
        if (n > 0) {
            SetIoError(ClientIoErrorKind::kNone, 0, "");
        } else if (n == 0) {
            SetIoError(ClientIoErrorKind::kClosed, 0, "tls peer closed");
        } else {
            const std::string tlsErr = TlsServerGetLastIoError();
            if (tlsErr.empty() || tlsErr == "no openssl error") {
                SetIoError(ClientIoErrorKind::kClosed, 0, "tls peer closed");
            } else {
                SetIoError(ClientIoErrorKind::kTlsError, 0, tlsErr);
            }
        }
        return n;
    }

    int n = send(client.socket, data, len, 0);
    if (n > 0) {
        SetIoError(ClientIoErrorKind::kNone, 0, "");
    } else if (n == 0) {
        SetIoError(ClientIoErrorKind::kClosed, 0, "tcp peer closed");
    } else {
        const int wsaErr = WSAGetLastError();
        if (wsaErr == WSAEWOULDBLOCK) {
            SetIoError(ClientIoErrorKind::kWouldBlock, wsaErr, "tcp send would block");
        } else {
            SetIoError(ClientIoErrorKind::kNetworkError, wsaErr,
                       "tcp send failed (wsa=" + std::to_string(wsaErr) + ")");
        }
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

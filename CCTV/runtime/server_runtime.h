#pragma once

#include <string>
#include <winsock2.h>

struct TlsServerConfig {
    bool enabled = false;
    bool requireClientCert = true;
    std::string caFile;
    std::string certFile;
    std::string keyFile;
    std::string sslDll = "libssl-1_1-x64.dll";
    std::string cryptoDll = "libcrypto-1_1-x64.dll";
};

struct ServerClient {
    SOCKET socket = INVALID_SOCKET;
    void* ssl = nullptr;  // SSL*
    bool secure = false;
};

struct ServerSocketContext {
    SOCKET server = INVALID_SOCKET;
    bool wsaStarted = false;
    bool secureEnabled = false;
    int acceptedClientTimeoutMs = 0;
    void* tlsCtx = nullptr;  // SSL_CTX*
};

enum class ClientIoErrorKind {
    kNone,
    kClosed,
    kWouldBlock,
    kNetworkError,
    kTlsError,
};

struct ClientIoErrorInfo {
    ClientIoErrorKind kind = ClientIoErrorKind::kNone;
    int code = 0;
    std::string detail;
};

bool InitServerSocket(int port, int backlog, const std::string& bindAddress,
                      const TlsServerConfig* tlsCfg, ServerSocketContext& ctx);
bool AcceptServerClient(ServerSocketContext& ctx, ServerClient& client, sockaddr_in* clientAddr, int* clientLen);
int ClientRecv(const ServerClient& client, char* buf, int len);
int ClientSend(const ServerClient& client, const char* data, int len);
ClientIoErrorInfo GetLastClientIoError();
void CloseServerClient(ServerClient& client);
void ShutdownServerSocket(ServerSocketContext& ctx);

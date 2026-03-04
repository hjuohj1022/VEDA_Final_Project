#include <filesystem>
#include <iostream>
#include <string>

#include <winsock2.h>
#include <windows.h>

#include "logging.h"
#include "server_runtime.h"

namespace {
constexpr int SSL_FILETYPE_PEM = 1;
constexpr int SSL_VERIFY_PEER = 0x01;
constexpr int SSL_VERIFY_FAIL_IF_NO_PEER_CERT = 0x02;

struct ssl_ctx_st;
struct ssl_st;
struct ssl_method_st;
using SSL_CTX = ssl_ctx_st;
using SSL = ssl_st;
using SSL_METHOD = ssl_method_st;

struct OpenSslApi {
    HMODULE sslLib = nullptr;
    HMODULE cryptoLib = nullptr;

    int (*SSL_library_init)() = nullptr;
    void (*SSL_load_error_strings)() = nullptr;
    void (*OpenSSL_add_all_algorithms)() = nullptr;
    int (*OPENSSL_init_ssl)(uint64_t, const void*) = nullptr;
    const SSL_METHOD* (*TLS_server_method)() = nullptr;
    SSL_CTX* (*SSL_CTX_new)(const SSL_METHOD*) = nullptr;
    void (*SSL_CTX_free)(SSL_CTX*) = nullptr;
    int (*SSL_CTX_use_certificate_file)(SSL_CTX*, const char*, int) = nullptr;
    int (*SSL_CTX_use_PrivateKey_file)(SSL_CTX*, const char*, int) = nullptr;
    int (*SSL_CTX_check_private_key)(const SSL_CTX*) = nullptr;
    int (*SSL_CTX_load_verify_locations)(SSL_CTX*, const char*, const char*) = nullptr;
    void (*SSL_CTX_set_verify)(SSL_CTX*, int, int (*)(int, void*)) = nullptr;
    SSL* (*SSL_new)(SSL_CTX*) = nullptr;
    void (*SSL_free)(SSL*) = nullptr;
    int (*SSL_set_fd)(SSL*, int) = nullptr;
    int (*SSL_accept)(SSL*) = nullptr;
    int (*SSL_read)(SSL*, void*, int) = nullptr;
    int (*SSL_write)(SSL*, const void*, int) = nullptr;
    int (*SSL_shutdown)(SSL*) = nullptr;
    int (*SSL_get_error)(const SSL*, int) = nullptr;
    unsigned long (*ERR_get_error)() = nullptr;
    void (*ERR_error_string_n)(unsigned long, char*, size_t) = nullptr;
};

OpenSslApi gSsl{};
bool gSslLoaded = false;

template <typename T>
bool LoadProc(HMODULE mod, const char* name, T& out) {
    out = reinterpret_cast<T>(GetProcAddress(mod, name));
    return out != nullptr;
}

std::string ResolveRuntimePath(const std::string& configuredPath) {
    namespace fs = std::filesystem;
    fs::path p(configuredPath);
    if (p.is_absolute()) return p.string();

    std::error_code ec;
    fs::path cwdCandidate = fs::current_path() / p;
    if (fs::exists(cwdCandidate, ec)) return cwdCandidate.string();

    fs::path base = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        fs::path candidate = base / p;
        if (fs::exists(candidate, ec)) return candidate.string();
        if (!base.has_parent_path()) break;
        base = base.parent_path();
    }
    return configuredPath;
}

std::string GetOpenSslLastError() {
    if (!gSsl.ERR_get_error || !gSsl.ERR_error_string_n) return "openssl error unavailable";
    const unsigned long e = gSsl.ERR_get_error();
    if (e == 0) return "no openssl error";
    char buf[256] = {0};
    gSsl.ERR_error_string_n(e, buf, sizeof(buf));
    return std::string(buf);
}

bool TryLoadOpenSslDlls(const std::string& sslDll, const std::string& cryptoDll) {
    gSsl.sslLib = LoadLibraryA(sslDll.c_str());
    gSsl.cryptoLib = LoadLibraryA(cryptoDll.c_str());

    if (!gSsl.sslLib || !gSsl.cryptoLib) {
        const char* gitBin = "C:\\Program Files\\Git\\mingw64\\bin\\";
        std::string sslFallback = std::string(gitBin) + sslDll;
        std::string cryptoFallback = std::string(gitBin) + cryptoDll;
        if (!gSsl.sslLib) gSsl.sslLib = LoadLibraryA(sslFallback.c_str());
        if (!gSsl.cryptoLib) gSsl.cryptoLib = LoadLibraryA(cryptoFallback.c_str());
    }
    return gSsl.sslLib && gSsl.cryptoLib;
}

bool LoadOpenSsl(const TlsServerConfig& tlsCfg) {
    if (gSslLoaded) return true;
    if (!TryLoadOpenSslDlls(tlsCfg.sslDll, tlsCfg.cryptoDll)) {
        LogError("Failed to load OpenSSL DLLs (" + tlsCfg.sslDll + ", " + tlsCfg.cryptoDll + ")");
        return false;
    }

    // Optional init symbols: OpenSSL 1.1+ may not export legacy APIs.
    LoadProc(gSsl.sslLib, "SSL_library_init", gSsl.SSL_library_init);
    LoadProc(gSsl.sslLib, "SSL_load_error_strings", gSsl.SSL_load_error_strings);
    LoadProc(gSsl.sslLib, "OpenSSL_add_all_algorithms", gSsl.OpenSSL_add_all_algorithms);
    LoadProc(gSsl.sslLib, "OPENSSL_init_ssl", gSsl.OPENSSL_init_ssl);

    bool ok = true;
    auto needSsl = [&](const char* name, auto& fn) {
        if (!LoadProc(gSsl.sslLib, name, fn)) {
            LogError(std::string("Missing OpenSSL symbol (ssl): ") + name);
            ok = false;
        }
    };
    auto needCrypto = [&](const char* name, auto& fn) {
        if (!LoadProc(gSsl.cryptoLib, name, fn)) {
            LogError(std::string("Missing OpenSSL symbol (crypto): ") + name);
            ok = false;
        }
    };

    needSsl("TLS_server_method", gSsl.TLS_server_method);
    needSsl("SSL_CTX_new", gSsl.SSL_CTX_new);
    needSsl("SSL_CTX_free", gSsl.SSL_CTX_free);
    needSsl("SSL_CTX_use_certificate_file", gSsl.SSL_CTX_use_certificate_file);
    needSsl("SSL_CTX_use_PrivateKey_file", gSsl.SSL_CTX_use_PrivateKey_file);
    needSsl("SSL_CTX_check_private_key", gSsl.SSL_CTX_check_private_key);
    needSsl("SSL_CTX_load_verify_locations", gSsl.SSL_CTX_load_verify_locations);
    needSsl("SSL_CTX_set_verify", gSsl.SSL_CTX_set_verify);
    needSsl("SSL_new", gSsl.SSL_new);
    needSsl("SSL_free", gSsl.SSL_free);
    needSsl("SSL_set_fd", gSsl.SSL_set_fd);
    needSsl("SSL_accept", gSsl.SSL_accept);
    needSsl("SSL_read", gSsl.SSL_read);
    needSsl("SSL_write", gSsl.SSL_write);
    needSsl("SSL_shutdown", gSsl.SSL_shutdown);
    needSsl("SSL_get_error", gSsl.SSL_get_error);
    needCrypto("ERR_get_error", gSsl.ERR_get_error);
    needCrypto("ERR_error_string_n", gSsl.ERR_error_string_n);
    if (!ok) {
        LogError("Failed to resolve OpenSSL API symbols.");
        return false;
    }

    if (gSsl.OPENSSL_init_ssl) {
        gSsl.OPENSSL_init_ssl(0, nullptr);
    } else {
        if (gSsl.SSL_library_init) gSsl.SSL_library_init();
        if (gSsl.SSL_load_error_strings) gSsl.SSL_load_error_strings();
        if (gSsl.OpenSSL_add_all_algorithms) gSsl.OpenSSL_add_all_algorithms();
    }
    gSslLoaded = true;
    return true;
}
}  // namespace

bool InitServerSocket(int port, int backlog, const TlsServerConfig* tlsCfg, ServerSocketContext& ctx) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return false;
    }
    ctx.wsaStarted = true;

    if (tlsCfg && tlsCfg->enabled) {
        if (!LoadOpenSsl(*tlsCfg)) return false;
        SSL_CTX* sslCtx = gSsl.SSL_CTX_new(gSsl.TLS_server_method());
        if (!sslCtx) {
            LogError("SSL_CTX_new failed: " + GetOpenSslLastError());
            return false;
        }

        const std::string certPath = ResolveRuntimePath(tlsCfg->certFile);
        const std::string keyPath = ResolveRuntimePath(tlsCfg->keyFile);
        const std::string caPath = ResolveRuntimePath(tlsCfg->caFile);

        if (gSsl.SSL_CTX_use_certificate_file(sslCtx, certPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LogError("SSL_CTX_use_certificate_file failed: " + certPath + " err=" + GetOpenSslLastError());
            gSsl.SSL_CTX_free(sslCtx);
            return false;
        }
        if (gSsl.SSL_CTX_use_PrivateKey_file(sslCtx, keyPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LogError("SSL_CTX_use_PrivateKey_file failed: " + keyPath + " err=" + GetOpenSslLastError());
            gSsl.SSL_CTX_free(sslCtx);
            return false;
        }
        if (gSsl.SSL_CTX_check_private_key(sslCtx) != 1) {
            LogError("SSL_CTX_check_private_key failed: " + GetOpenSslLastError());
            gSsl.SSL_CTX_free(sslCtx);
            return false;
        }
        if (gSsl.SSL_CTX_load_verify_locations(sslCtx, caPath.c_str(), nullptr) != 1) {
            LogError("SSL_CTX_load_verify_locations failed: " + caPath + " err=" + GetOpenSslLastError());
            gSsl.SSL_CTX_free(sslCtx);
            return false;
        }

        int verifyMode = SSL_VERIFY_PEER;
        if (tlsCfg->requireClientCert) verifyMode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        gSsl.SSL_CTX_set_verify(sslCtx, verifyMode, nullptr);

        ctx.tlsCtx = sslCtx;
        ctx.secureEnabled = true;
        LogInfo("Control mTLS enabled. cert=" + certPath + " ca=" + caPath);
    }

    ctx.server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctx.server == INVALID_SOCKET) {
        std::cerr << "socket failed" << std::endl;
        WSACleanup();
        ctx.wsaStarted = false;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(ctx.server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind failed (port=" << port << ", wsa=" << WSAGetLastError() << ")" << std::endl;
        closesocket(ctx.server);
        ctx.server = INVALID_SOCKET;
        WSACleanup();
        ctx.wsaStarted = false;
        return false;
    }

    if (listen(ctx.server, backlog) == SOCKET_ERROR) {
        std::cerr << "listen failed" << std::endl;
        closesocket(ctx.server);
        ctx.server = INVALID_SOCKET;
        WSACleanup();
        ctx.wsaStarted = false;
        return false;
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

    SSL_CTX* sslCtx = reinterpret_cast<SSL_CTX*>(ctx.tlsCtx);
    SSL* ssl = gSsl.SSL_new(sslCtx);
    if (!ssl) {
        LogWarn("SSL_new failed: " + GetOpenSslLastError());
        closesocket(s);
        return false;
    }
    if (gSsl.SSL_set_fd(ssl, static_cast<int>(s)) != 1) {
        LogWarn("SSL_set_fd failed: " + GetOpenSslLastError());
        gSsl.SSL_free(ssl);
        closesocket(s);
        return false;
    }
    if (gSsl.SSL_accept(ssl) != 1) {
        LogWarn("SSL_accept failed: " + GetOpenSslLastError());
        gSsl.SSL_free(ssl);
        closesocket(s);
        return false;
    }

    client.ssl = ssl;
    client.secure = true;
    return true;
}

int ClientRecv(const ServerClient& client, char* buf, int len) {
    if (!client.secure) return recv(client.socket, buf, len, 0);
    return gSsl.SSL_read(reinterpret_cast<SSL*>(client.ssl), buf, len);
}

int ClientSend(const ServerClient& client, const char* data, int len) {
    if (!client.secure) return send(client.socket, data, len, 0);
    return gSsl.SSL_write(reinterpret_cast<SSL*>(client.ssl), data, len);
}

void CloseServerClient(ServerClient& client) {
    if (client.secure && client.ssl) {
        SSL* ssl = reinterpret_cast<SSL*>(client.ssl);
        gSsl.SSL_shutdown(ssl);
        gSsl.SSL_free(ssl);
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
    if (ctx.tlsCtx && gSsl.SSL_CTX_free) {
        gSsl.SSL_CTX_free(reinterpret_cast<SSL_CTX*>(ctx.tlsCtx));
        ctx.tlsCtx = nullptr;
    }
    if (ctx.wsaStarted) {
        WSACleanup();
        ctx.wsaStarted = false;
    }
}

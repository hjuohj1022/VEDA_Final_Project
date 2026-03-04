#include <filesystem>
#include <string>

#include <winsock2.h>
#include <windows.h>

#include "tls_server.h"

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
    unsigned long (*ERR_get_error)() = nullptr;
    void (*ERR_error_string_n)(unsigned long, char*, size_t) = nullptr;
};

OpenSslApi gSsl{};
bool gSslLoaded = false;
thread_local std::string gLastTlsIoError;

std::string MakeTlsError(const std::string& stage, const std::string& detail) {
    return "[TLS] " + stage + " failed: " + detail;
}

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

bool LoadOpenSsl(const TlsServerConfig& tlsCfg, std::string& outErr) {
    if (gSslLoaded) return true;
    if (!TryLoadOpenSslDlls(tlsCfg.sslDll, tlsCfg.cryptoDll)) {
        outErr = MakeTlsError("load_dll", "ssl=" + tlsCfg.sslDll + ", crypto=" + tlsCfg.cryptoDll);
        return false;
    }

    // Optional init symbols for compatibility.
    LoadProc(gSsl.sslLib, "SSL_library_init", gSsl.SSL_library_init);
    LoadProc(gSsl.sslLib, "SSL_load_error_strings", gSsl.SSL_load_error_strings);
    LoadProc(gSsl.sslLib, "OpenSSL_add_all_algorithms", gSsl.OpenSSL_add_all_algorithms);
    LoadProc(gSsl.sslLib, "OPENSSL_init_ssl", gSsl.OPENSSL_init_ssl);

    bool ok = true;
    auto needSsl = [&](const char* name, auto& fn) {
        if (!LoadProc(gSsl.sslLib, name, fn)) {
            outErr = MakeTlsError("resolve_symbol", std::string("module=ssl symbol=") + name);
            ok = false;
        }
    };
    auto needCrypto = [&](const char* name, auto& fn) {
        if (!LoadProc(gSsl.cryptoLib, name, fn)) {
            outErr = MakeTlsError("resolve_symbol", std::string("module=crypto symbol=") + name);
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
    needCrypto("ERR_get_error", gSsl.ERR_get_error);
    needCrypto("ERR_error_string_n", gSsl.ERR_error_string_n);
    if (!ok) return false;

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

bool TlsServerInit(const TlsServerConfig& cfg, void** outTlsCtx, std::string& outErr) {
    if (!outTlsCtx) return false;
    *outTlsCtx = nullptr;

    if (!LoadOpenSsl(cfg, outErr)) return false;
    SSL_CTX* sslCtx = gSsl.SSL_CTX_new(gSsl.TLS_server_method());
    if (!sslCtx) {
        outErr = MakeTlsError("ssl_ctx_new", "openssl_err=" + GetOpenSslLastError());
        return false;
    }

    const std::string certPath = ResolveRuntimePath(cfg.certFile);
    const std::string keyPath = ResolveRuntimePath(cfg.keyFile);
    const std::string caPath = ResolveRuntimePath(cfg.caFile);

    if (gSsl.SSL_CTX_use_certificate_file(sslCtx, certPath.c_str(), SSL_FILETYPE_PEM) != 1) {
        outErr =
            MakeTlsError("load_server_cert", "path=" + certPath + " openssl_err=" + GetOpenSslLastError());
        gSsl.SSL_CTX_free(sslCtx);
        return false;
    }
    if (gSsl.SSL_CTX_use_PrivateKey_file(sslCtx, keyPath.c_str(), SSL_FILETYPE_PEM) != 1) {
        outErr = MakeTlsError("load_server_key", "path=" + keyPath + " openssl_err=" + GetOpenSslLastError());
        gSsl.SSL_CTX_free(sslCtx);
        return false;
    }
    if (gSsl.SSL_CTX_check_private_key(sslCtx) != 1) {
        outErr = MakeTlsError("check_server_key_pair", "openssl_err=" + GetOpenSslLastError());
        gSsl.SSL_CTX_free(sslCtx);
        return false;
    }
    if (gSsl.SSL_CTX_load_verify_locations(sslCtx, caPath.c_str(), nullptr) != 1) {
        outErr = MakeTlsError("load_ca", "path=" + caPath + " openssl_err=" + GetOpenSslLastError());
        gSsl.SSL_CTX_free(sslCtx);
        return false;
    }

    int verifyMode = SSL_VERIFY_PEER;
    if (cfg.requireClientCert) verifyMode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    gSsl.SSL_CTX_set_verify(sslCtx, verifyMode, nullptr);

    *outTlsCtx = sslCtx;
    return true;
}

bool TlsServerAccept(void* tlsCtx, int socketFd, void** outSsl, std::string& outErr) {
    if (!tlsCtx || !outSsl) return false;
    *outSsl = nullptr;

    SSL_CTX* sslCtx = reinterpret_cast<SSL_CTX*>(tlsCtx);
    SSL* ssl = gSsl.SSL_new(sslCtx);
    if (!ssl) {
        outErr = MakeTlsError("ssl_new", "openssl_err=" + GetOpenSslLastError());
        return false;
    }
    if (gSsl.SSL_set_fd(ssl, socketFd) != 1) {
        outErr = MakeTlsError("ssl_set_fd", "openssl_err=" + GetOpenSslLastError());
        gSsl.SSL_free(ssl);
        return false;
    }
    if (gSsl.SSL_accept(ssl) != 1) {
        outErr = MakeTlsError("ssl_accept", "openssl_err=" + GetOpenSslLastError());
        gSsl.SSL_free(ssl);
        return false;
    }
    *outSsl = ssl;
    return true;
}

int TlsServerRecv(void* ssl, char* buf, int len) {
    int n = gSsl.SSL_read(reinterpret_cast<SSL*>(ssl), buf, len);
    if (n <= 0) {
        gLastTlsIoError = GetOpenSslLastError();
    } else {
        gLastTlsIoError.clear();
    }
    return n;
}

int TlsServerSend(void* ssl, const char* data, int len) {
    int n = gSsl.SSL_write(reinterpret_cast<SSL*>(ssl), data, len);
    if (n <= 0) {
        gLastTlsIoError = GetOpenSslLastError();
    } else {
        gLastTlsIoError.clear();
    }
    return n;
}

std::string TlsServerGetLastIoError() {
    return gLastTlsIoError;
}

void TlsServerCloseClient(void* ssl) {
    if (!ssl) return;
    SSL* s = reinterpret_cast<SSL*>(ssl);
    gSsl.SSL_shutdown(s);
    gSsl.SSL_free(s);
}

void TlsServerShutdown(void* tlsCtx) {
    if (!tlsCtx || !gSsl.SSL_CTX_free) return;
    gSsl.SSL_CTX_free(reinterpret_cast<SSL_CTX*>(tlsCtx));
}

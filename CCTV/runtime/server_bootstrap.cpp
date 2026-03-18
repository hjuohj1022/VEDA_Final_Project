#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "server_bootstrap.h"

namespace {
constexpr int kMinPort = 1;
constexpr int kMaxPort = 65535;
constexpr int kConfigSearchDepth = 6;
}  // namespace

std::string ResolveConfigPath(const std::string& configuredPath) {
    namespace fs = std::filesystem;
    fs::path p(configuredPath);
    if (p.is_absolute()) {
        return p.string();
    }

    std::error_code ec;
    fs::path cwdCandidate = fs::current_path() / p;
    if (fs::exists(cwdCandidate, ec)) {
        return cwdCandidate.string();
    }

    fs::path base = fs::current_path();
    for (int i = 0; i < kConfigSearchDepth; ++i) {
        fs::path candidate = base / p;
        if (fs::exists(candidate, ec)) {
            return candidate.string();
        }
        if (!base.has_parent_path()) {
            break;
        }
        base = base.parent_path();
    }
    return configuredPath;
}

TlsServerConfig BuildTlsServerConfig(const RuntimeConfig& cfg, const bool disableControlTls) {
    TlsServerConfig tlsCfg;
    tlsCfg.enabled = cfg.control_tls.enabled && !disableControlTls;
    tlsCfg.requireClientCert = cfg.control_tls.require_client_cert;
    tlsCfg.caFile = cfg.control_tls.ca_file;
    tlsCfg.certFile = cfg.control_tls.cert_file;
    tlsCfg.keyFile = cfg.control_tls.key_file;
    tlsCfg.sslDll = cfg.control_tls.ssl_dll;
    tlsCfg.cryptoDll = cfg.control_tls.crypto_dll;
    return tlsCfg;
}

bool ValidateServerStartupConfig(const RuntimeConfig& cfg, const int port,
                                 const TlsServerConfig& tlsCfg, std::string& outErr) {
    if ((port < kMinPort) || (port > kMaxPort)) {
        outErr = "[CFG] invalid --port value: " + std::to_string(port) + " (expected 1..65535)";
        return false;
    }
    if (cfg.server_listen_backlog <= 0) {
        outErr = "[CFG] invalid server_listen_backlog: " + std::to_string(cfg.server_listen_backlog);
        return false;
    }

    if (!tlsCfg.enabled) {
        return true;
    }

    const std::vector<std::pair<std::string, std::string>> requiredFiles = {
        {"control_tls.ca_file", tlsCfg.caFile},
        {"control_tls.cert_file", tlsCfg.certFile},
        {"control_tls.key_file", tlsCfg.keyFile},
    };
    for (const auto& item : requiredFiles) {
        const std::string resolved = ResolveConfigPath(item.second);
        std::error_code ec;
        if (!std::filesystem::exists(resolved, ec)) {
            outErr = "[CFG] missing file: " + item.first + "=" + item.second;
            return false;
        }
    }
    if (tlsCfg.sslDll.empty() || tlsCfg.cryptoDll.empty()) {
        outErr = "[CFG] control_tls ssl/crypto dll name must not be empty";
        return false;
    }
    return true;
}

#include "runtime_config.h"

#include <cstdlib>

namespace {
int ParsePositiveEnvOrDefault(const char* name, const int defaultValue) {
    char* raw = nullptr;
    std::size_t rawLen = 0;
    if (_dupenv_s(&raw, &rawLen, name) != 0 || !raw || rawLen == 0) {
        free(raw);
        return defaultValue;
    }

    char* end = nullptr;
    const long parsed = std::strtol(raw, &end, 10);
    free(raw);
    if (end == raw || (end && *end != '\0') || parsed <= 0) {
        return defaultValue;
    }
    return static_cast<int>(parsed);
}

RuntimeConfig BuildRuntimeConfig() {
    RuntimeConfig cfg;
    cfg.control_client_read_timeout_ms =
        ParsePositiveEnvOrDefault("VEDA_CONTROL_CLIENT_READ_TIMEOUT_MS", cfg.control_client_read_timeout_ms);
    cfg.proxy_relay_io_timeout_ms =
        ParsePositiveEnvOrDefault("VEDA_PROXY_RELAY_IO_TIMEOUT_MS", cfg.proxy_relay_io_timeout_ms);
    cfg.proxy_max_concurrent_clients =
        ParsePositiveEnvOrDefault("VEDA_PROXY_MAX_CONCURRENT_CLIENTS", cfg.proxy_max_concurrent_clients);
    return cfg;
}
}  // namespace

const RuntimeConfig& GetRuntimeConfig() {
    static const RuntimeConfig cfg = BuildRuntimeConfig();
    return cfg;
}

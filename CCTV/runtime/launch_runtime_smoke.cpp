#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "launch_options.h"
#include "runtime_config.h"

namespace {
int failures = 0;

void Expect(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << std::endl;
        ++failures;
    }
}

std::optional<std::string> ReadEnvVar(const char* name) {
    char* raw = nullptr;
    std::size_t rawLen = 0;
    if (_dupenv_s(&raw, &rawLen, name) != 0 || !raw || rawLen == 0) {
        free(raw);
        return std::nullopt;
    }
    std::string value(raw);
    free(raw);
    return value;
}

void SetEnvVar(const char* name, const std::optional<std::string>& value) {
    if (value.has_value()) {
        _putenv_s(name, value->c_str());
        return;
    }
    _putenv_s(name, "");
}

class ScopedEnvRestore {
public:
    ScopedEnvRestore(const char* name, std::optional<std::string> newValue)
        : name_(name), oldValue_(ReadEnvVar(name)) {
        SetEnvVar(name_, newValue);
    }

    ~ScopedEnvRestore() {
        SetEnvVar(name_, oldValue_);
    }

    ScopedEnvRestore(const ScopedEnvRestore&) = delete;
    ScopedEnvRestore& operator=(const ScopedEnvRestore&) = delete;

private:
    const char* name_;
    std::optional<std::string> oldValue_;
};

void TestResolveWorkerPort() {
    LaunchOptions options;
    options.port = 9090;
    Expect(ResolveWorkerPort(options) == 9091, "default worker port should be port+1");

    options.workerPort = 19100;
    Expect(ResolveWorkerPort(options) == 19100, "explicit worker port should be used as-is");

    options.workerPort = 0;
    options.port = 65535;
    Expect(ResolveWorkerPort(options) == 65534, "max port should fall back to port-1");
}

void TestValidateLaunchOptions() {
    std::string error;
    LaunchOptions proxyOptions;
    proxyOptions.port = 19130;
    proxyOptions.workerPort = 19130;
    Expect(!ValidateLaunchOptions(proxyOptions, error), "proxy mode should reject duplicate port/worker-port");
    Expect(error.find("--worker-port must differ from --port") != std::string::npos,
           "duplicate port error message should be descriptive");

    error.clear();
    LaunchOptions workerOptions = proxyOptions;
    workerOptions.workerMode = true;
    Expect(ValidateLaunchOptions(workerOptions, error), "worker mode should ignore worker-port conflict");
}

void TestParseLaunchOptions() {
    std::vector<std::string> args = {
        "depth_trt.exe",
        "--port=19140",
        "--worker-port=19141",
        "--plain-control",
        "--bind=127.0.0.1",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }

    LaunchOptions parsed;
    std::string error;
    Expect(ParseLaunchOptions(static_cast<int>(argv.size()), argv.data(), parsed, error),
           "launch options should parse valid arguments");
    Expect(parsed.port == 19140, "parsed port should match argument");
    Expect(parsed.workerPort == 19141, "parsed worker port should match argument");
    Expect(parsed.plainControl, "plain control flag should be set");
    Expect(parsed.bindAddress == "127.0.0.1", "bind address should parse");
}

void TestRuntimeConfigDefaults() {
    ScopedEnvRestore readTimeout("VEDA_CONTROL_CLIENT_READ_TIMEOUT_MS", std::nullopt);
    ScopedEnvRestore relayTimeout("VEDA_PROXY_RELAY_IO_TIMEOUT_MS", std::nullopt);
    ScopedEnvRestore maxClients("VEDA_PROXY_MAX_CONCURRENT_CLIENTS", std::nullopt);
    ScopedEnvRestore workerReadyWait("VEDA_WORKER_READY_WAIT_MS", std::nullopt);

    const RuntimeConfig cfg = LoadRuntimeConfigFromEnvironment();
    Expect(cfg.control_client_read_timeout_ms == 5000, "default control client read timeout should be 5000ms");
    Expect(cfg.proxy_relay_io_timeout_ms == 30000, "default proxy relay timeout should be 30000ms");
    Expect(cfg.proxy_max_concurrent_clients == 32, "default proxy max concurrent clients should be 32");
    Expect(cfg.worker_ready_wait_ms == 25000, "default worker ready wait should be 25000ms");
}

void TestRuntimeConfigEnvOverrides() {
    ScopedEnvRestore readTimeout("VEDA_CONTROL_CLIENT_READ_TIMEOUT_MS", std::string("7000"));
    ScopedEnvRestore relayTimeout("VEDA_PROXY_RELAY_IO_TIMEOUT_MS", std::string("45000"));
    ScopedEnvRestore maxClients("VEDA_PROXY_MAX_CONCURRENT_CLIENTS", std::string("5"));
    ScopedEnvRestore workerReadyWait("VEDA_WORKER_READY_WAIT_MS", std::string("12000"));

    const RuntimeConfig cfg = LoadRuntimeConfigFromEnvironment();
    Expect(cfg.control_client_read_timeout_ms == 7000, "control client read timeout env override should apply");
    Expect(cfg.proxy_relay_io_timeout_ms == 45000, "proxy relay timeout env override should apply");
    Expect(cfg.proxy_max_concurrent_clients == 5, "proxy max concurrent clients env override should apply");
    Expect(cfg.worker_ready_wait_ms == 12000, "worker ready wait env override should apply");
}

void TestRuntimeConfigInvalidEnvFallback() {
    ScopedEnvRestore readTimeout("VEDA_CONTROL_CLIENT_READ_TIMEOUT_MS", std::string("0"));
    ScopedEnvRestore relayTimeout("VEDA_PROXY_RELAY_IO_TIMEOUT_MS", std::string("-1"));
    ScopedEnvRestore maxClients("VEDA_PROXY_MAX_CONCURRENT_CLIENTS", std::string("abc"));
    ScopedEnvRestore workerReadyWait("VEDA_WORKER_READY_WAIT_MS", std::string("invalid"));

    const RuntimeConfig cfg = LoadRuntimeConfigFromEnvironment();
    Expect(cfg.control_client_read_timeout_ms == 5000, "non-positive control timeout should fall back to default");
    Expect(cfg.proxy_relay_io_timeout_ms == 30000, "invalid relay timeout should fall back to default");
    Expect(cfg.proxy_max_concurrent_clients == 32, "invalid max client count should fall back to default");
    Expect(cfg.worker_ready_wait_ms == 25000, "invalid worker ready wait should fall back to default");
}
}  // namespace

int main() {
    TestResolveWorkerPort();
    TestValidateLaunchOptions();
    TestParseLaunchOptions();
    TestRuntimeConfigDefaults();
    TestRuntimeConfigEnvOverrides();
    TestRuntimeConfigInvalidEnvFallback();

    if (failures > 0) {
        std::cerr << "launch_runtime_smoke failed: " << failures << std::endl;
        return 1;
    }
    std::cout << "launch_runtime_smoke passed" << std::endl;
    return 0;
}

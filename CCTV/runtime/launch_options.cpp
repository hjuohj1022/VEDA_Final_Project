#include <string>
#include <string_view>

#include "launch_options.h"
#include "request.h"

namespace {
constexpr int kDefaultPort = 9090;
constexpr int kMinPort = 1;
constexpr int kMaxPort = 65535;
constexpr std::string_view kPortPrefix = "--port=";
constexpr std::string_view kWorkerPortPrefix = "--worker-port=";
constexpr std::string_view kBindPrefix = "--bind=";

bool ParseBoundedPort(const std::string& text, const char* flagName, int& outPort, std::string& outErr) {
    int parsed = 0;
    if (!ParseInt(text, parsed)) {
        outErr = std::string("[CFG] invalid ") + flagName + " value: " + text;
        return false;
    }
    if ((parsed < kMinPort) || (parsed > kMaxPort)) {
        outErr = std::string("[CFG] invalid ") + flagName + " value: " + text + " (expected 1..65535)";
        return false;
    }
    outPort = parsed;
    return true;
}
}  // namespace

bool ParseLaunchOptions(int argc, char** argv, LaunchOptions& out, std::string& outErr) {
    out = LaunchOptions{};
    out.port = kDefaultPort;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--worker") {
            out.workerMode = true;
            continue;
        }
        if (arg == "--plain-control") {
            out.plainControl = true;
            continue;
        }
        if (arg.rfind(kPortPrefix.data(), 0) == 0) {
            if (!ParseBoundedPort(arg.substr(kPortPrefix.size()), "--port", out.port, outErr)) {
                return false;
            }
            continue;
        }
        if (arg.rfind(kWorkerPortPrefix.data(), 0) == 0) {
            if (!ParseBoundedPort(arg.substr(kWorkerPortPrefix.size()), "--worker-port", out.workerPort, outErr)) {
                return false;
            }
            continue;
        }
        if (arg.rfind(kBindPrefix.data(), 0) == 0) {
            out.bindAddress = arg.substr(kBindPrefix.size());
            continue;
        }
    }

    return true;
}

int ResolveWorkerPort(const LaunchOptions& options) {
    if (options.workerPort > 0) {
        return options.workerPort;
    }
    if (options.port < kMaxPort) {
        return options.port + 1;
    }
    if (options.port > kMinPort) {
        return options.port - 1;
    }
    return kDefaultPort + 1;
}

bool ValidateLaunchOptions(const LaunchOptions& options, std::string& outErr) {
    if (options.workerMode) {
        return true;
    }

    const int resolvedWorkerPort = ResolveWorkerPort(options);
    if (resolvedWorkerPort == options.port) {
        outErr = "[CFG] --worker-port must differ from --port (both resolved to " +
                 std::to_string(options.port) + ")";
        return false;
    }

    return true;
}

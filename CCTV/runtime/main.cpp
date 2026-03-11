#include <string>

#include "launch_options.h"
#include "logging.h"
#include "proxy_server.h"
#include "service_app.h"

int main(int argc, char** argv) {
    LaunchOptions options;
    std::string parseErr;
    if (!ParseLaunchOptions(argc, argv, options, parseErr)) {
        LogError(parseErr);
        return 1;
    }

    if (options.workerMode) {
        ServiceRunOptions serviceOptions;
        serviceOptions.port = options.port;
        serviceOptions.disableControlTls = options.plainControl;
        serviceOptions.bindAddress = options.bindAddress;
        return RunDepthService(serviceOptions);
    }

    ProxyRunOptions proxyOptions;
    proxyOptions.port = options.port;
    proxyOptions.workerPort = ResolveWorkerPort(options);
    proxyOptions.disableControlTls = options.plainControl;
    proxyOptions.bindAddress = options.bindAddress;
    return RunProxyServer(proxyOptions);
}

#pragma once

#include <string>

struct ProxyRunOptions {
    int port = 9090;
    int workerPort = 9091;
    bool disableControlTls = false;
    std::string bindAddress;
};

int RunProxyServer(const ProxyRunOptions& options);

#pragma once

#include <string>

struct ServiceRunOptions {
    int port = 9090;
    bool disableControlTls = false;
    std::string bindAddress;
};

int RunDepthService(const ServiceRunOptions& options);

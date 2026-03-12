#pragma once

#include <string>

struct LaunchOptions {
    int port = 9090;
    bool workerMode = false;
    int workerPort = 0;
    bool plainControl = false;
    std::string bindAddress;
};

bool ParseLaunchOptions(int argc, char** argv, LaunchOptions& out, std::string& outErr);
bool ValidateLaunchOptions(const LaunchOptions& options, std::string& outErr);
int ResolveWorkerPort(const LaunchOptions& options);

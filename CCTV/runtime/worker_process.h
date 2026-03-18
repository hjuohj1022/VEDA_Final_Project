#pragma once

#include <mutex>
#include <string>

#include <winsock2.h>
#include <windows.h>

class WorkerProcessManager {
public:
    explicit WorkerProcessManager(int port);
    ~WorkerProcessManager();

    int port() const noexcept { return port_; }

    bool IsRunning();
    bool EnsureRunning(std::string& outErr);
    void Shutdown();

private:
    bool SpawnLocked(std::string& outErr);
    bool WaitUntilReadyLocked(std::string& outErr);
    void TerminateAndCleanupLocked(DWORD waitMs = 2000) noexcept;
    void CleanupHandlesLocked() noexcept;

    std::wstring exePath_;
    std::wstring workDir_;
    int port_ = 0;
    PROCESS_INFORMATION procInfo_{};
    bool hasProcess_ = false;
    std::mutex mu_;
};

bool ConnectToWorker(int port, SOCKET& outSocket, std::string& outErr, DWORD timeoutMs = 1500);
bool SendAllToSocket(SOCKET socket, const char* data, int len, std::string& outErr);

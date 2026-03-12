#include <filesystem>
#include <string>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "logging.h"
#include "worker_process.h"

namespace {
constexpr DWORD kWorkerStartupTimeoutMs = 5000;
constexpr DWORD kWorkerConnectRetrySleepMs = 100;
constexpr const char* kWorkerHost = "127.0.0.1";

std::wstring GetCurrentExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = 0;
    while (true) {
        len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) {
            return L"";
        }
        if (len < path.size() - 1) {
            path.resize(len);
            return path;
        }
        path.resize(path.size() * 2U);
    }
}

std::wstring GetParentDirectory(const std::wstring& path) {
    return std::filesystem::path(path).parent_path().wstring();
}
}  // namespace

bool SendAllToSocket(const SOCKET socket, const char* data, const int len, std::string& outErr) {
    int offset = 0;
    while (offset < len) {
        const int sent = send(socket, data + offset, len - offset, 0);
        if (sent <= 0) {
            outErr = "worker send failed (wsa=" + std::to_string(WSAGetLastError()) + ")";
            return false;
        }
        offset += sent;
    }
    return true;
}

bool ConnectToWorker(const int port, SOCKET& outSocket, std::string& outErr, const DWORD timeoutMs) {
    outSocket = INVALID_SOCKET;

    const DWORD startTick = GetTickCount();
    while (true) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            outErr = "worker socket failed (wsa=" + std::to_string(WSAGetLastError()) + ")";
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        if (inet_pton(AF_INET, kWorkerHost, &addr.sin_addr) != 1) {
            closesocket(sock);
            outErr = "worker address parse failed";
            return false;
        }

        if (connect(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != SOCKET_ERROR) {
            outSocket = sock;
            return true;
        }

        const int wsaErr = WSAGetLastError();
        closesocket(sock);

        if (GetTickCount() - startTick >= timeoutMs) {
            outErr = "worker connect failed (port=" + std::to_string(port) +
                     ", wsa=" + std::to_string(wsaErr) + ")";
            return false;
        }
        Sleep(kWorkerConnectRetrySleepMs);
    }
}

WorkerProcessManager::WorkerProcessManager(const int port)
    : exePath_(GetCurrentExecutablePath()),
      workDir_(GetParentDirectory(exePath_)),
      port_(port) {}

WorkerProcessManager::~WorkerProcessManager() {
    Shutdown();
}

bool WorkerProcessManager::IsRunning() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!hasProcess_) {
        return false;
    }
    const DWORD waitResult = WaitForSingleObject(procInfo_.hProcess, 0);
    if (waitResult == WAIT_TIMEOUT) {
        return true;
    }
    CleanupHandlesLocked();
    return false;
}

bool WorkerProcessManager::EnsureRunning(std::string& outErr) {
    std::lock_guard<std::mutex> lock(mu_);
    if (hasProcess_) {
        const DWORD waitResult = WaitForSingleObject(procInfo_.hProcess, 0);
        if (waitResult == WAIT_TIMEOUT) {
            return true;
        }
        CleanupHandlesLocked();
    }

    if (!SpawnLocked(outErr)) {
        return false;
    }
    return WaitUntilReadyLocked(outErr);
}

void WorkerProcessManager::Shutdown() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!hasProcess_) {
        return;
    }

    TerminateAndCleanupLocked();
}

bool WorkerProcessManager::SpawnLocked(std::string& outErr) {
    if (exePath_.empty()) {
        outErr = "worker executable path resolution failed";
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    const std::wstring commandLine = L"\"" + exePath_ + L"\" --worker --port=" +
                                     std::to_wstring(port_) +
                                     L" --bind=127.0.0.1 --plain-control";
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    if (!CreateProcessW(exePath_.c_str(), mutableCommand.data(),
                        nullptr, nullptr, FALSE, 0, nullptr,
                        workDir_.empty() ? nullptr : workDir_.c_str(),
                        &si, &pi)) {
        outErr = "worker spawn failed (gle=" + std::to_string(GetLastError()) + ")";
        return false;
    }

    procInfo_ = pi;
    hasProcess_ = true;
    LogInfo("[PROXY] spawned worker process on 127.0.0.1:" + std::to_string(port_));
    return true;
}

bool WorkerProcessManager::WaitUntilReadyLocked(std::string& outErr) {
    const DWORD startTick = GetTickCount();
    while (true) {
        if (!hasProcess_) {
            outErr = "worker process handle missing";
            return false;
        }

        const DWORD waitResult = WaitForSingleObject(procInfo_.hProcess, 0);
        if (waitResult != WAIT_TIMEOUT) {
            DWORD exitCode = 0;
            GetExitCodeProcess(procInfo_.hProcess, &exitCode);
            outErr = "worker exited during startup (exit=" + std::to_string(exitCode) + ")";
            CleanupHandlesLocked();
            return false;
        }

        SOCKET readySocket = INVALID_SOCKET;
        std::string connectErr;
        if (ConnectToWorker(port_, readySocket, connectErr, 50)) {
            closesocket(readySocket);
            return true;
        }

        if (GetTickCount() - startTick >= kWorkerStartupTimeoutMs) {
            outErr = "worker startup timeout on port " + std::to_string(port_);
            TerminateAndCleanupLocked();
            return false;
        }
        Sleep(kWorkerConnectRetrySleepMs);
    }
}

void WorkerProcessManager::TerminateAndCleanupLocked(const DWORD waitMs) noexcept {
    if (!hasProcess_) {
        return;
    }

    if (procInfo_.hProcess != nullptr) {
        TerminateProcess(procInfo_.hProcess, 0);
        WaitForSingleObject(procInfo_.hProcess, waitMs);
    }
    CleanupHandlesLocked();
}

void WorkerProcessManager::CleanupHandlesLocked() noexcept {
    if (procInfo_.hThread != nullptr) {
        CloseHandle(procInfo_.hThread);
        procInfo_.hThread = nullptr;
    }
    if (procInfo_.hProcess != nullptr) {
        CloseHandle(procInfo_.hProcess);
        procInfo_.hProcess = nullptr;
    }
    procInfo_.dwProcessId = 0;
    procInfo_.dwThreadId = 0;
    hasProcess_ = false;
}

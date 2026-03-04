#include <iostream>
#include <string>
#include <thread>
#include <atomic>

// Windows networking
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// 설정 파일 포함 (.gitignore로 관리됨)
#include "app_config.h"
#include "command_dispatcher.h"
#include "logging.h"
#include "request.h"
#include "runtime_config.h"
#include "server_runtime.h"

int main(int argc, char** argv) {
    const RuntimeConfig& cfg = GetRuntimeConfig();
    int port = 9090;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--port=", 0) == 0) {
            port = std::stoi(arg.substr(7));
        }
    }

    ServerSocketContext serverCtx;
    if (!InitServerSocket(port, cfg.server_listen_backlog, serverCtx)) {
        return 1;
    }

    LogInfo("Listening on port " + std::to_string(port));

    std::thread worker;
    std::thread streamThread;
    std::atomic<bool> workerStop{false};
    DepthStreamBuffer depthStream;
    std::thread rgbdStreamThread;
    RgbdStreamBuffer rgbdStream;
    bool workerRunning = false;
    std::atomic<bool> streamActive{false};
    std::atomic<bool> rgbdStreamActive{false};
    std::thread pcStreamThread;
    ImageStreamBuffer pcStream;
    std::atomic<bool> pcStreamActive{false};
    ViewParams viewParams;
    ServerRuntimeContext runtimeCtx{
        worker,
        streamThread,
        rgbdStreamThread,
        pcStreamThread,
        workerStop,
        streamActive,
        rgbdStreamActive,
        pcStreamActive,
        workerRunning,
        depthStream,
        rgbdStream,
        pcStream,
        viewParams,
    };

    while (true) {
        JoinFinishedStreamThreads(runtimeCtx);
        sockaddr_in clientAddr{};
        int clientLen = sizeof(clientAddr);
        SOCKET client = accept(serverCtx.server, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (client == INVALID_SOCKET) continue;

        char buf[1024];
        int len = recv(client, buf, sizeof(buf) - 1, 0);
        if (len <= 0) {
            LogWarn("Client connected but sent no data");
            closesocket(client);
            continue;
        }
        buf[len] = '\0';

        std::string line(buf);
        {
            char ip[64] = {0};
            inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
            LogInfo(std::string("Request from ") + ip + ":" +
                    std::to_string(ntohs(clientAddr.sin_port)) + " -> " + line);
        }
        Request req = ParseRequest(line);
        HandleClientRequest(client, req, runtimeCtx);
    }

    ShutdownRuntime(runtimeCtx);
    ShutdownServerSocket(serverCtx);
    return 0;
}

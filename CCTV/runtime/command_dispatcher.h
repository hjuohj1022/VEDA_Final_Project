#pragma once

#include <atomic>
#include <thread>

#include "request.h"
#include "runtime_types.h"
#include "server_runtime.h"

struct ServerRuntimeContext {
    std::thread& worker;
    std::thread& depthStreamThread;
    std::thread& rgbdStreamThread;
    std::thread& pcStreamThread;
    std::atomic<bool>& workerStop;
    std::atomic<bool>& depthStreamActive;
    std::atomic<bool>& rgbdStreamActive;
    std::atomic<bool>& pcStreamActive;
    bool& workerRunning;
    DepthStreamBuffer& depthStream;
    RgbdStreamBuffer& rgbdStream;
    ImageStreamBuffer& pcStream;
    ViewParams& viewParams;
    WorkerControlState& controlState;
};

void JoinFinishedStreamThreads(ServerRuntimeContext& ctx);
void ShutdownRuntime(ServerRuntimeContext& ctx);
void HandleClientRequest(ServerClient client, const Request& req, ServerRuntimeContext& ctx);

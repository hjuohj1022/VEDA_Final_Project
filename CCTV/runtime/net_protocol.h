#pragma once

#include <atomic>
#include <string>

#include "server_runtime.h"
#include "runtime_types.h"

void SendResponse(const ServerClient& client, const std::string& msg);

void DepthStreamWorker(ServerClient client, DepthStreamBuffer* streamBuf, std::atomic<bool>* active);
void RgbdStreamWorker(ServerClient client, RgbdStreamBuffer* streamBuf, std::atomic<bool>* active);
void PcImageStreamWorker(ServerClient client, ImageStreamBuffer* streamBuf, std::atomic<bool>* active);

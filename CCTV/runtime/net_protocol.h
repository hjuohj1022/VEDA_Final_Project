#pragma once

#include <atomic>
#include <string>

#include <winsock2.h>

#include "runtime_types.h"

void SendResponse(SOCKET client, const std::string& msg);

void DepthStreamWorker(SOCKET client, DepthStreamBuffer* streamBuf, std::atomic<bool>* active);
void RgbdStreamWorker(SOCKET client, RgbdStreamBuffer* streamBuf, std::atomic<bool>* active);
void PcImageStreamWorker(SOCKET client, ImageStreamBuffer* streamBuf, std::atomic<bool>* active);

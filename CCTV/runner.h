#pragma once

#include <atomic>

struct DepthStreamBuffer;
struct ImageStreamBuffer;
struct ViewParams;
bool RunDepthWorker(int channel, bool headless, std::atomic<bool>& stopFlag,
                    DepthStreamBuffer* streamBuf,
                    ImageStreamBuffer* pcStreamBuf,
                    ViewParams* viewParams);

#pragma once

#include <atomic>

struct DepthStreamBuffer;
struct RgbdStreamBuffer;
struct ImageStreamBuffer;
struct ViewParams;
bool RunDepthWorker(int channel, bool headless, std::atomic<bool>& stopFlag,
                    DepthStreamBuffer* streamBuf,
                    RgbdStreamBuffer* rgbdStreamBuf,
                    ImageStreamBuffer* pcStreamBuf,
                    ViewParams* viewParams);

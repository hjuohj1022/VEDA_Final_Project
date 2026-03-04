#pragma once

#include <atomic>

#include "runtime_types.h"

bool RunDepthWorker(int channel, bool headless, std::atomic<bool>& stopFlag,
                    DepthStreamBuffer* streamBuf,
                    RgbdStreamBuffer* rgbdStreamBuf,
                    ImageStreamBuffer* pcStreamBuf,
                    ViewParams* viewParams);

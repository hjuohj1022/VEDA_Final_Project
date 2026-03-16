#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

#include "runtime_types.h"

struct WorkerStartupState {
    std::mutex mu;
    std::condition_variable cv;
    bool finished = false;
    bool success = false;
    std::string detail;
};

bool RunDepthWorker(int channel, bool headless, std::atomic<bool>& stopFlag,
                    DepthStreamBuffer* streamBuf,
                    RgbdStreamBuffer* rgbdStreamBuf,
                    ImageStreamBuffer* pcStreamBuf,
                    ViewParams* viewParams,
                    WorkerControlState* controlState,
                    WorkerStartupState* startupState = nullptr);

#pragma once

#include <atomic>

bool RunDepthWorker(int channel, bool headless, std::atomic<bool>& stopFlag);

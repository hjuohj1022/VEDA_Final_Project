#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

#include <winsock2.h>

struct DepthStreamBuffer {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<float> data;
    int width = 0;
    int height = 0;
    uint32_t frameIdx = 0;
    bool hasFrame = false;
    bool stop = false;
    SOCKET activeSocket = INVALID_SOCKET;
};

struct ImageStreamBuffer {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<unsigned char> data;
    int width = 0;
    int height = 0;
    uint32_t frameIdx = 0;
    bool hasFrame = false;
    bool stop = false;
    SOCKET activeSocket = INVALID_SOCKET;
};

struct RgbdStreamBuffer {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<float> depth;
    std::vector<unsigned char> bgr;
    int width = 0;
    int height = 0;
    uint32_t frameIdx = 0;
    bool hasFrame = false;
    bool stop = false;
    SOCKET activeSocket = INVALID_SOCKET;
};

struct ViewParams {
    std::mutex mu;
    float rotX = -20.0f;
    float rotY = 35.0f;
    bool flipX = false;
    bool flipY = false;
    bool flipZ = false;
    bool wire = false;
    bool mesh = false;
};

struct WorkerControlState {
    std::mutex mu;
    bool paused = false;
};

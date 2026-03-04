#include <atomic>
#include <string>
#include <vector>

#include "logging.h"
#include "net_protocol.h"

namespace {
std::string FormatIoFailure(const char* op) {
    const ClientIoErrorInfo ioErr = GetLastClientIoError();
    if (ioErr.detail.empty()) return std::string(op) + " failed";
    return std::string(op) + " failed: " + ioErr.detail;
}

bool SendAll(const ServerClient& client, const char* data, int bytes) {
    int offset = 0;
    while (offset < bytes) {
        int n = ClientSend(client, data + offset, bytes - offset);
        if (n <= 0) {
            LogWarn("[NET] " + FormatIoFailure("send"));
            return false;
        }
        offset += n;
    }
    return true;
}
}  // namespace

void SendResponse(const ServerClient& client, const std::string& msg) {
    SendAll(client, msg.c_str(), static_cast<int>(msg.size()));
}

void DepthStreamWorker(ServerClient client, DepthStreamBuffer* streamBuf, std::atomic<bool>* active) {
    if (!streamBuf) {
        CloseServerClient(client);
        return;
    }

    LogInfo("Depth stream client connected.");
    if (active) active->store(true);
    streamBuf->stop = false;

    std::string ok = "OK depth_stream\n";
    if (!SendAll(client, ok.c_str(), static_cast<int>(ok.size()))) {
        if (active) active->store(false);
        CloseServerClient(client);
        return;
    }

    while (true) {
        std::vector<float> local;
        int w = 0, h = 0;
        uint32_t frameIdx = 0;
        {
            std::unique_lock<std::mutex> lock(streamBuf->mu);
            streamBuf->cv.wait(lock, [&] { return streamBuf->hasFrame || streamBuf->stop; });
            if (streamBuf->stop) break;
            local = streamBuf->data;
            w = streamBuf->width;
            h = streamBuf->height;
            frameIdx = streamBuf->frameIdx;
            streamBuf->hasFrame = false;
        }

        const uint32_t payloadBytes = static_cast<uint32_t>(local.size() * sizeof(float));
        uint32_t header[4] = {frameIdx, static_cast<uint32_t>(w), static_cast<uint32_t>(h), payloadBytes};
        if (!SendAll(client, reinterpret_cast<const char*>(header), static_cast<int>(sizeof(header)))) break;
        if (!SendAll(client, reinterpret_cast<const char*>(local.data()), static_cast<int>(payloadBytes))) break;
    }

    LogWarn("Depth stream client disconnected.");
    if (active) active->store(false);
    CloseServerClient(client);
}

void RgbdStreamWorker(ServerClient client, RgbdStreamBuffer* streamBuf, std::atomic<bool>* active) {
    if (!streamBuf) {
        CloseServerClient(client);
        return;
    }

    LogInfo("RGBD stream client connected.");
    if (active) active->store(true);
    streamBuf->stop = false;

    std::string ok = "OK rgbd_stream fmt=depth32f+bgr24\n";
    if (!SendAll(client, ok.c_str(), static_cast<int>(ok.size()))) {
        if (active) active->store(false);
        CloseServerClient(client);
        return;
    }

    while (true) {
        std::vector<float> localDepth;
        std::vector<unsigned char> localBgr;
        int w = 0, h = 0;
        uint32_t frameIdx = 0;
        {
            std::unique_lock<std::mutex> lock(streamBuf->mu);
            streamBuf->cv.wait(lock, [&] { return streamBuf->hasFrame || streamBuf->stop; });
            if (streamBuf->stop) break;
            localDepth = streamBuf->depth;
            localBgr = streamBuf->bgr;
            w = streamBuf->width;
            h = streamBuf->height;
            frameIdx = streamBuf->frameIdx;
            streamBuf->hasFrame = false;
        }

        const uint32_t depthBytes = static_cast<uint32_t>(localDepth.size() * sizeof(float));
        const uint32_t bgrBytes = static_cast<uint32_t>(localBgr.size());
        uint32_t header[5] = {frameIdx, static_cast<uint32_t>(w), static_cast<uint32_t>(h), depthBytes, bgrBytes};
        if (!SendAll(client, reinterpret_cast<const char*>(header), static_cast<int>(sizeof(header)))) break;
        if (!SendAll(client, reinterpret_cast<const char*>(localDepth.data()), static_cast<int>(depthBytes))) break;
        if (!SendAll(client, reinterpret_cast<const char*>(localBgr.data()), static_cast<int>(bgrBytes))) break;
    }

    LogWarn("RGBD stream client disconnected.");
    if (active) active->store(false);
    CloseServerClient(client);
}

void PcImageStreamWorker(ServerClient client, ImageStreamBuffer* streamBuf, std::atomic<bool>* active) {
    if (!streamBuf) {
        CloseServerClient(client);
        return;
    }

    LogInfo("PC image stream client connected.");
    if (active) active->store(true);
    streamBuf->stop = false;

    std::string ok = "OK pc_stream fmt=png\n";
    if (!SendAll(client, ok.c_str(), static_cast<int>(ok.size()))) {
        if (active) active->store(false);
        CloseServerClient(client);
        return;
    }

    while (true) {
        std::vector<unsigned char> local;
        int w = 0, h = 0;
        uint32_t frameIdx = 0;
        {
            std::unique_lock<std::mutex> lock(streamBuf->mu);
            streamBuf->cv.wait(lock, [&] { return streamBuf->hasFrame || streamBuf->stop; });
            if (streamBuf->stop) break;
            local = streamBuf->data;
            w = streamBuf->width;
            h = streamBuf->height;
            frameIdx = streamBuf->frameIdx;
            streamBuf->hasFrame = false;
        }

        const uint32_t payloadBytes = static_cast<uint32_t>(local.size());
        uint32_t header[4] = {frameIdx, static_cast<uint32_t>(w), static_cast<uint32_t>(h), payloadBytes};
        if (!SendAll(client, reinterpret_cast<const char*>(header), static_cast<int>(sizeof(header)))) break;
        if (!SendAll(client, reinterpret_cast<const char*>(local.data()), static_cast<int>(payloadBytes))) break;
    }

    LogWarn("PC image stream client disconnected.");
    if (active) active->store(false);
    CloseServerClient(client);
}

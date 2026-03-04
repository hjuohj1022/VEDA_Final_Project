#include <atomic>
#include <string>
#include <vector>

#include <winsock2.h>

#include "logging.h"
#include "net_protocol.h"

void SendResponse(SOCKET client, const std::string& msg) {
    send(client, msg.c_str(), static_cast<int>(msg.size()), 0);
}

void DepthStreamWorker(SOCKET client, DepthStreamBuffer* streamBuf, std::atomic<bool>* active) {
    if (!streamBuf) {
        closesocket(client);
        return;
    }

    LogInfo("Depth stream client connected.");
    if (active) active->store(true);
    streamBuf->stop = false;

    std::string ok = "OK depth_stream\n";
    send(client, ok.c_str(), static_cast<int>(ok.size()), 0);

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
        int sent = send(client, reinterpret_cast<const char*>(header), sizeof(header), 0);
        if (sent <= 0) break;
        sent = send(client, reinterpret_cast<const char*>(local.data()), payloadBytes, 0);
        if (sent <= 0) break;
    }

    LogWarn("Depth stream client disconnected.");
    if (active) active->store(false);
    closesocket(client);
}

void RgbdStreamWorker(SOCKET client, RgbdStreamBuffer* streamBuf, std::atomic<bool>* active) {
    if (!streamBuf) {
        closesocket(client);
        return;
    }

    LogInfo("RGBD stream client connected.");
    if (active) active->store(true);
    streamBuf->stop = false;

    std::string ok = "OK rgbd_stream fmt=depth32f+bgr24\n";
    send(client, ok.c_str(), static_cast<int>(ok.size()), 0);

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
        int sent = send(client, reinterpret_cast<const char*>(header), sizeof(header), 0);
        if (sent <= 0) break;
        sent = send(client, reinterpret_cast<const char*>(localDepth.data()), depthBytes, 0);
        if (sent <= 0) break;
        sent = send(client, reinterpret_cast<const char*>(localBgr.data()), bgrBytes, 0);
        if (sent <= 0) break;
    }

    LogWarn("RGBD stream client disconnected.");
    if (active) active->store(false);
    closesocket(client);
}

void PcImageStreamWorker(SOCKET client, ImageStreamBuffer* streamBuf, std::atomic<bool>* active) {
    if (!streamBuf) {
        closesocket(client);
        return;
    }

    LogInfo("PC image stream client connected.");
    if (active) active->store(true);
    streamBuf->stop = false;

    std::string ok = "OK pc_stream fmt=png\n";
    send(client, ok.c_str(), static_cast<int>(ok.size()), 0);

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
        int sent = send(client, reinterpret_cast<const char*>(header), sizeof(header), 0);
        if (sent <= 0) break;
        sent = send(client, reinterpret_cast<const char*>(local.data()), payloadBytes, 0);
        if (sent <= 0) break;
    }

    LogWarn("PC image stream client disconnected.");
    if (active) active->store(false);
    closesocket(client);
}

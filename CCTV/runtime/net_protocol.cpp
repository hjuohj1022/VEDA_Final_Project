#include <atomic>
#include <array>
#include <limits>
#include <string>
#include <vector>

#include "logging.h"
#include "net_protocol.h"

namespace {
template <typename T>
bool TryConvertSizeToInt(const T value, int& out) {
    if (value > static_cast<T>((std::numeric_limits<int>::max)())) {
        return false;
    }

    out = static_cast<int>(value);
    return true;
}

std::string FormatIoFailure(const char* op) {
    const ClientIoErrorInfo ioErr = GetLastClientIoError();
    if (ioErr.detail.empty()) {
        return std::string(op) + " failed";
    }
    return std::string(op) + " failed: " + ioErr.detail;
}

bool SendAll(const ServerClient& client, const char* data, const int bytes) {
    int offset = 0;
    while (offset < bytes) {
        const int n = ClientSend(client, data + offset, bytes - offset);
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
    if (active) {
        active->store(true);
    }
    streamBuf->stop = false;

    const std::string ok = "OK depth_stream\n";
    int okMessageSize = 0;
    if (!TryConvertSizeToInt(ok.size(), okMessageSize) || !SendAll(client, ok.c_str(), okMessageSize)) {
        if (active) {
            active->store(false);
        }
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
        const std::array<uint32_t, 4U> header = {
            frameIdx,
            static_cast<uint32_t>(w),
            static_cast<uint32_t>(h),
            payloadBytes,
        };
        int headerSize = 0;
        int payloadSize = 0;
        if (!TryConvertSizeToInt(sizeof(header), headerSize) ||
            !TryConvertSizeToInt(payloadBytes, payloadSize) ||
            !SendAll(client, reinterpret_cast<const char*>(header.data()), headerSize) ||
            !SendAll(client, reinterpret_cast<const char*>(local.data()), payloadSize)) {
            break;
        }
    }

    LogWarn("Depth stream client disconnected.");
    if (active) {
        active->store(false);
    }
    CloseServerClient(client);
}

void RgbdStreamWorker(ServerClient client, RgbdStreamBuffer* streamBuf, std::atomic<bool>* active) {
    if (!streamBuf) {
        CloseServerClient(client);
        return;
    }

    LogInfo("RGBD stream client connected.");
    if (active) {
        active->store(true);
    }
    streamBuf->stop = false;

    const std::string ok = "OK rgbd_stream fmt=depth32f+bgr24\n";
    int okMessageSize = 0;
    if (!TryConvertSizeToInt(ok.size(), okMessageSize) || !SendAll(client, ok.c_str(), okMessageSize)) {
        if (active) {
            active->store(false);
        }
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
        const std::array<uint32_t, 5U> header = {
            frameIdx,
            static_cast<uint32_t>(w),
            static_cast<uint32_t>(h),
            depthBytes,
            bgrBytes,
        };
        int headerSize = 0;
        int depthPayloadSize = 0;
        int bgrPayloadSize = 0;
        if (!TryConvertSizeToInt(sizeof(header), headerSize) ||
            !TryConvertSizeToInt(depthBytes, depthPayloadSize) ||
            !TryConvertSizeToInt(bgrBytes, bgrPayloadSize) ||
            !SendAll(client, reinterpret_cast<const char*>(header.data()), headerSize) ||
            !SendAll(client, reinterpret_cast<const char*>(localDepth.data()), depthPayloadSize) ||
            !SendAll(client, reinterpret_cast<const char*>(localBgr.data()), bgrPayloadSize)) {
            break;
        }
    }

    LogWarn("RGBD stream client disconnected.");
    if (active) {
        active->store(false);
    }
    CloseServerClient(client);
}

void PcImageStreamWorker(ServerClient client, ImageStreamBuffer* streamBuf, std::atomic<bool>* active) {
    if (!streamBuf) {
        CloseServerClient(client);
        return;
    }

    LogInfo("PC image stream client connected.");
    if (active) {
        active->store(true);
    }
    streamBuf->stop = false;

    const std::string ok = "OK pc_stream fmt=png\n";
    int okMessageSize = 0;
    if (!TryConvertSizeToInt(ok.size(), okMessageSize) || !SendAll(client, ok.c_str(), okMessageSize)) {
        if (active) {
            active->store(false);
        }
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
        const std::array<uint32_t, 4U> header = {
            frameIdx,
            static_cast<uint32_t>(w),
            static_cast<uint32_t>(h),
            payloadBytes,
        };
        int headerSize = 0;
        int payloadSize = 0;
        if (!TryConvertSizeToInt(sizeof(header), headerSize) ||
            !TryConvertSizeToInt(payloadBytes, payloadSize) ||
            !SendAll(client, reinterpret_cast<const char*>(header.data()), headerSize) ||
            !SendAll(client, reinterpret_cast<const char*>(local.data()), payloadSize)) {
            break;
        }
    }

    LogWarn("PC image stream client disconnected.");
    if (active) {
        active->store(false);
    }
    CloseServerClient(client);
}

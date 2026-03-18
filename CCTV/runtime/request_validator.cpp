#include "request_validator.h"

namespace {
constexpr int kMinChannel = -1;
constexpr int kMaxChannel = 3;
constexpr int kMaxConcurrentStreamRequests = 1;

int CountRequestedStreams(const Request& req) {
    int streamReqCount = 0;
    if (req.depthStream) {
        ++streamReqCount;
    }
    if (req.rgbdStream) {
        ++streamReqCount;
    }
    if (req.pcStream) {
        ++streamReqCount;
    }
    return streamReqCount;
}
}  // namespace

RequestValidationResult ValidateRequest(const Request& req) {
    if ((req.channel < kMinChannel) || (req.channel > kMaxChannel)) {
        return {false, "ERR invalid channel\n"};
    }

    if (CountRequestedStreams(req) > kMaxConcurrentStreamRequests) {
        return {false, "ERR multiple stream requests\n"};
    }

    return {true, ""};
}

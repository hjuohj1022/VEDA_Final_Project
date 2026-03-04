#include "request_validator.h"

RequestValidationResult ValidateRequest(const Request& req) {
    if (req.channel < -1 || req.channel > 3) {
        return {false, "ERR invalid channel\n"};
    }

    int streamReqCount = 0;
    if (req.depthStream) ++streamReqCount;
    if (req.rgbdStream) ++streamReqCount;
    if (req.pcStream) ++streamReqCount;
    if (streamReqCount > 1) {
        return {false, "ERR multiple stream requests\n"};
    }

    return {true, ""};
}

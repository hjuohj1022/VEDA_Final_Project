#include <cstdlib>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#include "request.h"

std::vector<std::string> SplitTokens(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

bool ParseInt(const std::string& s, int& out) {
    char* end = nullptr;
    long val = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = static_cast<int>(val);
    return true;
}

static bool ParseFloat(const std::string& s, float& out) {
    char* end = nullptr;
    float val = std::strtof(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return false;
    out = val;
    return true;
}

static bool ParseBool(const std::string& s, bool& out) {
    std::string v;
    v.reserve(s.size());
    for (char c : s) {
        v.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (v == "1" || v == "true" || v == "on" || v == "yes") {
        out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "off" || v == "no") {
        out = false;
        return true;
    }
    return false;
}

Request ParseRequest(const std::string& line) {
    Request req;
    auto tokens = SplitTokens(line);
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if (t == "depth_stream" || t == "stream_depth") {
            req.depthStream = true;
            continue;
        }
        if (t == "rgbd_stream" || t == "depth_rgb_stream" || t == "stream_rgbd") {
            req.rgbdStream = true;
            continue;
        }
        if (t == "pc_stream" || t == "stream_pc") {
            req.pcStream = true;
            continue;
        }
        if (t == "pc_view" || t == "view_pc") {
            req.pcView = true;
            continue;
        }
        if (t.rfind("rx=", 0) == 0) {
            float v = 0.0f;
            if (ParseFloat(t.substr(3), v)) {
                req.rx = v;
                req.rxSet = true;
            }
            continue;
        }
        if (t.rfind("ry=", 0) == 0) {
            float v = 0.0f;
            if (ParseFloat(t.substr(3), v)) {
                req.ry = v;
                req.rySet = true;
            }
            continue;
        }
        if (t.rfind("rotX=", 0) == 0) {
            float v = 0.0f;
            if (ParseFloat(t.substr(5), v)) {
                req.rx = v;
                req.rxSet = true;
            }
            continue;
        }
        if (t.rfind("rotY=", 0) == 0) {
            float v = 0.0f;
            if (ParseFloat(t.substr(5), v)) {
                req.ry = v;
                req.rySet = true;
            }
            continue;
        }
        if (t.rfind("flipx=", 0) == 0) {
            bool v = false;
            if (ParseBool(t.substr(6), v)) {
                req.flipX = v;
                req.flipXSet = true;
            }
            continue;
        }
        if (t.rfind("flipy=", 0) == 0) {
            bool v = false;
            if (ParseBool(t.substr(6), v)) {
                req.flipY = v;
                req.flipYSet = true;
            }
            continue;
        }
        if (t.rfind("flipz=", 0) == 0) {
            bool v = false;
            if (ParseBool(t.substr(6), v)) {
                req.flipZ = v;
                req.flipZSet = true;
            }
            continue;
        }
        if (t.rfind("wire=", 0) == 0) {
            bool v = false;
            if (ParseBool(t.substr(5), v)) {
                req.wire = v;
                req.wireSet = true;
            }
            continue;
        }
        if (t.rfind("mesh=", 0) == 0) {
            bool v = false;
            if (ParseBool(t.substr(5), v)) {
                req.mesh = v;
                req.meshSet = true;
            }
            continue;
        }
        if (t == "pause") {
            req.pause = true;
            req.pauseSet = true;
            continue;
        }
        if (t == "resume") {
            req.pause = false;
            req.pauseSet = true;
            continue;
        }
        if (t.rfind("pause=", 0) == 0) {
            bool v = false;
            if (ParseBool(t.substr(6), v)) {
                req.pause = v;
                req.pauseSet = true;
            }
            continue;
        }
        if (t == "stop") {
            req.stop = true;
            continue;
        }
        if (t == "headless") {
            req.headless = true;
            req.headlessSet = true;
            continue;
        }
        if (t.rfind("headless=", 0) == 0) {
            bool v = false;
            if (ParseBool(t.substr(9), v)) {
                req.headless = v;
                req.headlessSet = true;
            }
            continue;
        }
        if (t == "gui") {
            req.gui = true;
            req.headlessSet = true;
            req.headless = false;
            continue;
        }
        if (t.rfind("gui=", 0) == 0) {
            bool v = false;
            if (ParseBool(t.substr(4), v) && v) {
                req.gui = true;
                req.headlessSet = true;
                req.headless = false;
            }
            continue;
        }
        if (t.rfind("channel=", 0) == 0) {
            int ch = -1;
            if (ParseInt(t.substr(8), ch)) req.channel = ch;
            continue;
        }
        if (t == "channel" && i + 1 < tokens.size()) {
            int ch = -1;
            if (ParseInt(tokens[i + 1], ch)) req.channel = ch;
            ++i;
            continue;
        }
        int ch = -1;
        if (ParseInt(t, ch)) {
            req.channel = ch;
            continue;
        }
    }
    return req;
}

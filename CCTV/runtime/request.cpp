#include <charconv>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "request.h"

namespace {
constexpr std::string_view kPrefixRx = "rx=";
constexpr std::string_view kPrefixRy = "ry=";
constexpr std::string_view kPrefixRotX = "rotX=";
constexpr std::string_view kPrefixRotY = "rotY=";
constexpr std::string_view kPrefixFlipX = "flipx=";
constexpr std::string_view kPrefixFlipY = "flipy=";
constexpr std::string_view kPrefixFlipZ = "flipz=";
constexpr std::string_view kPrefixWire = "wire=";
constexpr std::string_view kPrefixMesh = "mesh=";
constexpr std::string_view kPrefixPause = "pause=";
constexpr std::string_view kPrefixHeadless = "headless=";
constexpr std::string_view kPrefixGui = "gui=";
constexpr std::string_view kPrefixChannel = "channel=";

bool StartsWith(const std::string& text, const std::string_view prefix) {
    return (text.size() >= prefix.size()) &&
           (text.compare(0U, prefix.size(), prefix.data()) == 0);
}

std::optional<float> ParseFloatValue(const std::string& text) {
    std::istringstream stream(text);
    float value = 0.0F;
    char trailing = '\0';
    if (!(stream >> value)) {
        return std::nullopt;
    }
    if (stream >> trailing) {
        return std::nullopt;
    }
    return value;
}

std::string ToLowerCopy(const std::string& text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char ch : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lowered;
}

bool ParseBoolValue(const std::string& text, bool& out) {
    const std::string lowered = ToLowerCopy(text);

    if ((lowered == "1") || (lowered == "true") || (lowered == "on") || (lowered == "yes")) {
        out = true;
        return true;
    }
    if ((lowered == "0") || (lowered == "false") || (lowered == "off") || (lowered == "no")) {
        out = false;
        return true;
    }
    return false;
}

bool TryApplyFloatOption(const std::string& token,
                         const std::string_view prefix,
                         float& destination,
                         bool& destinationSet) {
    if (!StartsWith(token, prefix)) {
        return false;
    }

    const std::optional<float> parsedValue = ParseFloatValue(token.substr(prefix.size()));
    if (parsedValue.has_value()) {
        destination = *parsedValue;
        destinationSet = true;
    }
    return true;
}

bool TryApplyBoolOption(const std::string& token,
                        const std::string_view prefix,
                        bool& destination,
                        bool& destinationSet) {
    if (!StartsWith(token, prefix)) {
        return false;
    }

    bool parsedValue = false;
    if (ParseBoolValue(token.substr(prefix.size()), parsedValue)) {
        destination = parsedValue;
        destinationSet = true;
    }
    return true;
}
}  // namespace

std::vector<std::string> SplitTokens(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

bool ParseInt(const std::string& s, int& out) {
    int value = 0;
    const char* const begin = s.data();
    const char* const end = begin + s.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if ((result.ec != std::errc{}) || (result.ptr != end)) {
        return false;
    }
    out = value;
    return true;
}

Request ParseRequest(const std::string& line) {
    Request req;
    const std::vector<std::string> tokens = SplitTokens(line);
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
        if (TryApplyFloatOption(t, kPrefixRx, req.rx, req.rxSet)) continue;

        if (TryApplyFloatOption(t, kPrefixRy, req.ry, req.rySet)) continue;

        if (TryApplyFloatOption(t, kPrefixRotX, req.rx, req.rxSet)) continue;

        if (TryApplyFloatOption(t, kPrefixRotY, req.ry, req.rySet)) continue;

        if (TryApplyBoolOption(t, kPrefixFlipX, req.flipX, req.flipXSet)) continue;

        if (TryApplyBoolOption(t, kPrefixFlipY, req.flipY, req.flipYSet)) continue;

        if (TryApplyBoolOption(t, kPrefixFlipZ, req.flipZ, req.flipZSet)) continue;

        if (TryApplyBoolOption(t, kPrefixWire, req.wire, req.wireSet)) continue;

        if (TryApplyBoolOption(t, kPrefixMesh, req.mesh, req.meshSet)) continue;

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
        if (TryApplyBoolOption(t, kPrefixPause, req.pause, req.pauseSet)) continue;

        if (t == "stop") {
            req.stop = true;
            continue;
        }
        if (t == "headless") {
            req.headless = true;
            req.headlessSet = true;
            continue;
        }
        if (TryApplyBoolOption(t, kPrefixHeadless, req.headless, req.headlessSet)) continue;

        if (t == "gui") {
            req.gui = true;
            req.headlessSet = true;
            req.headless = false;
            continue;
        }
        if (StartsWith(t, kPrefixGui)) {
            bool v = false;
            if (ParseBoolValue(t.substr(kPrefixGui.size()), v) && v) {
                req.gui = true;
                req.headlessSet = true;
                req.headless = false;
            }
            continue;
        }
        if (StartsWith(t, kPrefixChannel)) {
            int ch = -1;
            if (ParseInt(t.substr(kPrefixChannel.size()), ch)) {
                req.channel = ch;
            }
            continue;
        }
        if (t == "channel" && i + 1 < tokens.size()) {
            int ch = -1;
            if (ParseInt(tokens[i + 1], ch)) {
                req.channel = ch;
            }
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

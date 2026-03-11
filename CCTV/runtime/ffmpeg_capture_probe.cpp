#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include <opencv2/opencv.hpp>

#include "app_config.h"
#include "ffmpeg_capture.h"
#include "runtime_config.h"

namespace {
struct ProbeOptions {
    int channel = RTSP_CHANNEL;
    int readFrames = 1;
    std::string urlOverride;
};

void PrintUsage() {
    std::cout
        << "Usage: ffmpeg_capture_probe [options]\n"
        << "  --channel=<0-3>      RTSP channel index (default: app_config.h RTSP_CHANNEL)\n"
        << "  --url=<rtsps://...>  Override RTSP URL directly\n"
        << "  --read-frames=<n>    Frames to decode after open (default: 1)\n"
        << "  --help               Show this message\n";
}

bool ParseInt(const std::string& text, int& out) {
    try {
        std::size_t pos = 0;
        const int value = std::stoi(text, &pos);
        if (pos != text.size()) return false;
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool ParseArgs(const int argc, char** argv, ProbeOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return false;
        }
        if (StartsWith(arg, "--channel=")) {
            if (!ParseInt(arg.substr(10), options.channel)) {
                std::cerr << "[FFMPEG_PROBE][ERR] invalid channel: " << arg << "\n";
                return false;
            }
            continue;
        }
        if (StartsWith(arg, "--read-frames=")) {
            if (!ParseInt(arg.substr(14), options.readFrames) || options.readFrames < 0) {
                std::cerr << "[FFMPEG_PROBE][ERR] invalid read frame count: " << arg << "\n";
                return false;
            }
            continue;
        }
        if (StartsWith(arg, "--url=")) {
            options.urlOverride = arg.substr(6);
            continue;
        }
        std::cerr << "[FFMPEG_PROBE][ERR] unknown argument: " << arg << "\n";
        PrintUsage();
        return false;
    }
    return true;
}

std::string ResolveRtspUrl(const ProbeOptions& options) {
    if (!options.urlOverride.empty()) return options.urlOverride;
    if (options.channel < 0 || options.channel >= static_cast<int>(RTSP_URLS.size())) {
        throw std::runtime_error("channel out of range");
    }
    return RTSP_URLS[static_cast<std::size_t>(options.channel)];
}

void PrintMatInfo(const cv::Mat& frame, const int index) {
    std::cout << "[FFMPEG_PROBE] frame[" << index << "] "
              << frame.cols << "x" << frame.rows
              << " type=" << frame.type()
              << " channels=" << frame.channels()
              << " continuous=" << (frame.isContinuous() ? "true" : "false")
              << "\n";
}
}  // namespace

int main(int argc, char** argv) {
    ProbeOptions options;
    if (!ParseArgs(argc, argv, options)) {
        return 1;
    }

    const RuntimeConfig& cfg = GetRuntimeConfig();
    std::string rtspUrl;
    try {
        rtspUrl = ResolveRtspUrl(options);
    } catch (const std::exception& ex) {
        std::cerr << "[FFMPEG_PROBE][ERR] " << ex.what() << "\n";
        return 1;
    }

    FfmpegRtspCapture capture;
    std::string captureError;

    std::cout << "[FFMPEG_PROBE] url=" << rtspUrl << "\n";
    std::cout << "[FFMPEG_PROBE] ffmpeg_capture_options=" << cfg.ffmpeg_capture_options << "\n";
    std::cout << "[FFMPEG_PROBE] open_timeout_ms=" << cfg.open_timeout_ms
              << " read_timeout_ms=" << cfg.read_timeout_ms << "\n";

    const auto openStart = std::chrono::steady_clock::now();
    if (!capture.Open(rtspUrl, cfg, captureError)) {
        const auto openEnd = std::chrono::steady_clock::now();
        const auto openMs = std::chrono::duration_cast<std::chrono::milliseconds>(openEnd - openStart).count();
        std::cerr << "[FFMPEG_PROBE][ERR] open failed after " << openMs
                  << " ms: " << captureError << "\n";
        return 2;
    }
    const auto openEnd = std::chrono::steady_clock::now();
    const auto openMs = std::chrono::duration_cast<std::chrono::milliseconds>(openEnd - openStart).count();
    std::cout << "[FFMPEG_PROBE] open_ms=" << openMs << "\n";

    for (int i = 0; i < options.readFrames; ++i) {
        cv::Mat frame;
        const auto readStart = std::chrono::steady_clock::now();
        if (!capture.Read(frame, captureError)) {
            const auto readEnd = std::chrono::steady_clock::now();
            const auto readMs = std::chrono::duration_cast<std::chrono::milliseconds>(readEnd - readStart).count();
            std::cerr << "[FFMPEG_PROBE][ERR] read failed after " << readMs
                      << " ms: " << captureError << "\n";
            return 3;
        }
        const auto readEnd = std::chrono::steady_clock::now();
        const auto readMs = std::chrono::duration_cast<std::chrono::milliseconds>(readEnd - readStart).count();
        std::cout << "[FFMPEG_PROBE] read[" << i << "] elapsed_ms=" << readMs << "\n";
        PrintMatInfo(frame, i);
    }

    capture.Close();
    std::cout << "[FFMPEG_PROBE] success\n";
    return 0;
}

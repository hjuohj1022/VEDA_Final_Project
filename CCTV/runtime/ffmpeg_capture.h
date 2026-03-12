#pragma once

#include <memory>
#include <string>

namespace cv {
class Mat;
}

struct RuntimeConfig;

class FfmpegRtspCapture {
public:
    FfmpegRtspCapture();
    ~FfmpegRtspCapture();

    FfmpegRtspCapture(FfmpegRtspCapture&& other) noexcept;
    FfmpegRtspCapture& operator=(FfmpegRtspCapture&& other) noexcept;

    FfmpegRtspCapture(const FfmpegRtspCapture&) = delete;
    FfmpegRtspCapture& operator=(const FfmpegRtspCapture&) = delete;

    bool Open(const std::string& url, const RuntimeConfig& cfg, std::string& error);
    bool Reopen(std::string& error);
    bool Read(cv::Mat& frame, std::string& error);
    bool IsOpened() const;
    void Close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

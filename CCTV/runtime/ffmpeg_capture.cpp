#include "ffmpeg_capture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include <opencv2/core/mat.hpp>

#include "runtime_config.h"

namespace {
std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string ResolveRuntimePath(const std::string& configuredPath) {
    namespace fs = std::filesystem;

    fs::path path(configuredPath);
    if (path.is_absolute()) return path.string();

    std::error_code ec;
    fs::path base = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        const fs::path candidate = base / path;
        if (fs::exists(candidate, ec)) return candidate.string();
        if (!base.has_parent_path()) break;
        base = base.parent_path();
    }
    return configuredPath;
}

std::string AvErrorToString(const int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(code, buffer.data(), buffer.size());
    return std::string(buffer.data());
}

struct DictionaryGuard {
    AVDictionary* dict = nullptr;

    ~DictionaryGuard() {
        av_dict_free(&dict);
    }
};

void SetOption(DictionaryGuard& dict, const std::string& key, std::string value) {
    if (key == "ca_file" || key == "cert_file" || key == "key_file") {
        value = ResolveRuntimePath(value);
    }
    av_dict_set(&dict.dict, key.c_str(), value.c_str(), 0);
}

void ApplyCaptureOptions(const RuntimeConfig& cfg, DictionaryGuard& dict) {
    std::stringstream stream(cfg.ffmpeg_capture_options);
    std::string token;
    while (std::getline(stream, token, '|')) {
        const std::size_t sep = token.find(';');
        if (sep == std::string::npos) continue;

        const std::string key = Trim(token.substr(0, sep));
        const std::string value = Trim(token.substr(sep + 1));
        if (key.empty()) continue;

        SetOption(dict, key, value);
    }
}
}  // namespace

struct FfmpegRtspCapture::Impl {
    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    SwsContext* swsCtx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* decodedFrame = nullptr;
    int videoStreamIndex = -1;
    int openTimeoutMs = 0;
    int readTimeoutMs = 0;
    bool deadlineActive = false;
    std::chrono::steady_clock::time_point deadline{};
    std::string openedUrl;

    ~Impl() {
        Close();
    }

    static int InterruptCallback(void* opaque) {
        auto* self = static_cast<Impl*>(opaque);
        if (!self || !self->deadlineActive) return 0;
        return std::chrono::steady_clock::now() > self->deadline ? 1 : 0;
    }

    void ArmDeadline(const int timeoutMs) {
        if (timeoutMs <= 0) {
            deadlineActive = false;
            return;
        }
        deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        deadlineActive = true;
    }

    void DisarmDeadline() {
        deadlineActive = false;
    }

    void Close() {
        DisarmDeadline();

        if (packet) {
            av_packet_free(&packet);
        }
        if (decodedFrame) {
            av_frame_free(&decodedFrame);
        }
        if (swsCtx) {
            sws_freeContext(swsCtx);
            swsCtx = nullptr;
        }
        if (codecCtx) {
            avcodec_free_context(&codecCtx);
        }
        if (formatCtx) {
            avformat_close_input(&formatCtx);
        }

        videoStreamIndex = -1;
        openedUrl.clear();
    }

    bool IsOpened() const {
        return formatCtx != nullptr && codecCtx != nullptr && videoStreamIndex >= 0;
    }

    bool Open(const std::string& url, const RuntimeConfig& cfg, std::string& error) {
        Close();

        openTimeoutMs = cfg.open_timeout_ms;
        readTimeoutMs = cfg.read_timeout_ms;

        avformat_network_init();

        DictionaryGuard options;
        ApplyCaptureOptions(cfg, options);

        formatCtx = avformat_alloc_context();
        if (!formatCtx) {
            error = "avformat_alloc_context failed";
            return false;
        }
        formatCtx->interrupt_callback.callback = &Impl::InterruptCallback;
        formatCtx->interrupt_callback.opaque = this;

        ArmDeadline(openTimeoutMs);
        const int openErr = avformat_open_input(&formatCtx, url.c_str(), nullptr, &options.dict);
        DisarmDeadline();
        if (openErr < 0) {
            error = "avformat_open_input failed: " + AvErrorToString(openErr);
            Close();
            return false;
        }

        ArmDeadline(openTimeoutMs);
        const int infoErr = avformat_find_stream_info(formatCtx, nullptr);
        DisarmDeadline();
        if (infoErr < 0) {
            error = "avformat_find_stream_info failed: " + AvErrorToString(infoErr);
            Close();
            return false;
        }

        videoStreamIndex = av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoStreamIndex < 0) {
            error = "av_find_best_stream failed: " + AvErrorToString(videoStreamIndex);
            Close();
            return false;
        }

        AVStream* stream = formatCtx->streams[videoStreamIndex];
        const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder) {
            error = "avcodec_find_decoder failed for codec_id=" +
                    std::to_string(stream->codecpar->codec_id);
            Close();
            return false;
        }

        codecCtx = avcodec_alloc_context3(decoder);
        if (!codecCtx) {
            error = "avcodec_alloc_context3 failed";
            Close();
            return false;
        }

        const int paramErr = avcodec_parameters_to_context(codecCtx, stream->codecpar);
        if (paramErr < 0) {
            error = "avcodec_parameters_to_context failed: " + AvErrorToString(paramErr);
            Close();
            return false;
        }
        codecCtx->pkt_timebase = stream->time_base;

        const int codecErr = avcodec_open2(codecCtx, decoder, nullptr);
        if (codecErr < 0) {
            error = "avcodec_open2 failed: " + AvErrorToString(codecErr);
            Close();
            return false;
        }

        packet = av_packet_alloc();
        decodedFrame = av_frame_alloc();
        if (!packet || !decodedFrame) {
            error = "av_packet_alloc/av_frame_alloc failed";
            Close();
            return false;
        }

        openedUrl = url;
        error.clear();
        return true;
    }

    bool ConvertFrame(const AVFrame* src, cv::Mat& outFrame, std::string& error) {
        if (!src || src->width <= 0 || src->height <= 0) {
            error = "decoder returned invalid frame geometry";
            return false;
        }

        swsCtx = sws_getCachedContext(
            swsCtx,
            src->width,
            src->height,
            static_cast<AVPixelFormat>(src->format),
            src->width,
            src->height,
            AV_PIX_FMT_BGR24,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr);
        if (!swsCtx) {
            error = "sws_getCachedContext failed";
            return false;
        }

        outFrame.create(src->height, src->width, CV_8UC3);
        uint8_t* dstData[4] = {outFrame.data, nullptr, nullptr, nullptr};
        int dstLineSize[4] = {static_cast<int>(outFrame.step[0]), 0, 0, 0};

        const int scaledRows = sws_scale(
            swsCtx,
            src->data,
            src->linesize,
            0,
            src->height,
            dstData,
            dstLineSize);
        if (scaledRows != src->height) {
            error = "sws_scale returned " + std::to_string(scaledRows) +
                    " rows for height=" + std::to_string(src->height);
            return false;
        }

        error.clear();
        return true;
    }

    bool Read(cv::Mat& frame, std::string& error) {
        if (!IsOpened()) {
            error = "capture is not open";
            return false;
        }

        while (true) {
            ArmDeadline(readTimeoutMs);
            const int readErr = av_read_frame(formatCtx, packet);
            DisarmDeadline();

            if (readErr == AVERROR(EAGAIN)) {
                continue;
            }
            if (readErr == AVERROR_EOF) {
                error = "stream ended";
                return false;
            }
            if (readErr < 0) {
                error = "av_read_frame failed: " + AvErrorToString(readErr);
                return false;
            }

            if (packet->stream_index != videoStreamIndex) {
                av_packet_unref(packet);
                continue;
            }

            const int sendErr = avcodec_send_packet(codecCtx, packet);
            av_packet_unref(packet);
            if (sendErr < 0 && sendErr != AVERROR(EAGAIN)) {
                error = "avcodec_send_packet failed: " + AvErrorToString(sendErr);
                return false;
            }

            while (true) {
                const int recvErr = avcodec_receive_frame(codecCtx, decodedFrame);
                if (recvErr == AVERROR(EAGAIN)) {
                    break;
                }
                if (recvErr == AVERROR_EOF) {
                    error = "decoder reached EOF";
                    return false;
                }
                if (recvErr < 0) {
                    error = "avcodec_receive_frame failed: " + AvErrorToString(recvErr);
                    return false;
                }

                const bool ok = ConvertFrame(decodedFrame, frame, error);
                av_frame_unref(decodedFrame);
                if (!ok) {
                    return false;
                }
                return true;
            }
        }
    }
};

FfmpegRtspCapture::FfmpegRtspCapture() : impl_(std::make_unique<Impl>()) {}

FfmpegRtspCapture::~FfmpegRtspCapture() = default;

FfmpegRtspCapture::FfmpegRtspCapture(FfmpegRtspCapture&& other) noexcept = default;

FfmpegRtspCapture& FfmpegRtspCapture::operator=(FfmpegRtspCapture&& other) noexcept = default;

bool FfmpegRtspCapture::Open(const std::string& url, const RuntimeConfig& cfg, std::string& error) {
    return impl_->Open(url, cfg, error);
}

bool FfmpegRtspCapture::Read(cv::Mat& frame, std::string& error) {
    return impl_->Read(frame, error);
}

bool FfmpegRtspCapture::IsOpened() const {
    return impl_->IsOpened();
}

void FfmpegRtspCapture::Close() {
    impl_->Close();
}

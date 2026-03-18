#include <chrono>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <cuda_runtime_api.h>
#include <opencv2/opencv.hpp>

#include "app_config.h"
#include "ffmpeg_capture.h"
#include "logging.h"
#include "pointcloud.h"
#include "runner.h"
#include "runtime_config.h"
#include "trt_engine.h"

using namespace cv;

namespace {
bool CheckCuda(cudaError_t result, char const* const func, const char* const file, int const line) {
    if (result != cudaSuccess) {
        LogError("CUDA error at " + std::string(file) + ":" + std::to_string(line) +
                 " code=" + std::to_string(static_cast<int>(result)) + " \"" + func + "\"");
        return false;
    }
    return true;
}

void CompleteWorkerStartup(WorkerStartupState* startupState, const bool success, const std::string& detail) {
    if (!startupState) {
        return;
    }

    bool shouldNotify = false;
    {
        std::lock_guard<std::mutex> lock(startupState->mu);
        if (!startupState->finished) {
            startupState->finished = true;
            startupState->success = success;
            startupState->detail = detail;
            shouldNotify = true;
        }
    }
    if (shouldNotify) {
        startupState->cv.notify_all();
    }
}

constexpr int kReferenceWidth = 1920;
constexpr int kReferenceHeight = 1080;
constexpr float kReferenceHfovDeg = 109.0f;
constexpr float kReferenceVfovDeg = 55.0f;
constexpr const char* kMainWindowTitle = "Depth Anything 3 Metric (CUDA Optimized)";
constexpr const char* kPointCloudWindowTitle = "PointCloud (Projected)";

struct PointCloudViewConfig {
    float rotX = -20.0f;
    float rotY = 35.0f;
    bool flipX = false;
    bool flipY = false;
    bool flipZ = false;
    bool wire = false;
    bool mesh = false;
};

class WorkerPerfTracker {
public:
    explicit WorkerPerfTracker(const RuntimeConfig& cfg)
        : cfg_(cfg),
          previewFpsClock_(std::chrono::high_resolution_clock::now()),
          lastMetricsLog_(std::chrono::steady_clock::now()) {}

    double TickPreviewFps() {
        const auto now = std::chrono::high_resolution_clock::now();
        const double elapsedSec = std::chrono::duration<double>(now - previewFpsClock_).count();
        previewFpsClock_ = now;
        return elapsedSec > 0.0 ? 1.0 / elapsedSec : 0.0;
    }

    void RecordFrame(const double e2eMs, const double gpuMs) {
        PushWindow(e2eMsWindow_, e2eMsSum_, e2eMs);
        PushWindow(gpuMsWindow_, gpuMsSum_, gpuMs);
        processedFrames_++;
    }

    void MaybeEmit(const bool headless,
                   const uint64_t totalReadFail,
                   const uint64_t totalInvalidFrame,
                   const uint64_t totalReconnectSuccess,
                   const uint64_t totalReconnectFail) {
        const auto now = std::chrono::steady_clock::now();
        const double logIntervalSec = std::chrono::duration<double>(now - lastMetricsLog_).count();
        if (logIntervalSec < cfg_.metrics_log_interval_sec) {
            return;
        }

        const uint64_t intervalProcessed = processedFrames_ - processedFramesAtLastLog_;
        const uint64_t dropProxyNow = totalReadFail + totalInvalidFrame;
        const uint64_t intervalDropProxy = dropProxyNow - dropProxyAtLastLog_;
        const double procFps = intervalProcessed / logIntervalSec;
        const double avgE2eMs =
            e2eMsWindow_.empty() ? 0.0 : (e2eMsSum_ / static_cast<double>(e2eMsWindow_.size()));
        const double avgGpuMs =
            gpuMsWindow_.empty() ? 0.0 : (gpuMsSum_ / static_cast<double>(gpuMsWindow_.size()));
        const double dropProxyRatio =
            (intervalProcessed + intervalDropProxy) == 0
                ? 0.0
                : (100.0 * static_cast<double>(intervalDropProxy) /
                   static_cast<double>(intervalProcessed + intervalDropProxy));

        std::ostringstream oss1;
        oss1 << std::fixed << std::setprecision(1)
             << "Perf fps=" << procFps
             << " e2e_ms=" << avgE2eMs
             << " gpu_ms=" << avgGpuMs;

        std::ostringstream oss2;
        oss2 << std::fixed << std::setprecision(2)
             << "drop_proxy_pct=" << dropProxyRatio
             << " (readFail=" << totalReadFail
             << ", invalid=" << totalInvalidFrame
             << ", reopenOk=" << totalReconnectSuccess
             << ", reopenFail=" << totalReconnectFail << ")";

        overlay1_ = oss1.str();
        overlay2_ = oss2.str();
        if (headless) {
            LogInfo("[Perf] " + overlay1_ + " " + overlay2_);
        }

        lastMetricsLog_ = now;
        processedFramesAtLastLog_ = processedFrames_;
        dropProxyAtLastLog_ = dropProxyNow;
    }

    const std::string& Overlay1() const { return overlay1_; }
    const std::string& Overlay2() const { return overlay2_; }

private:
    void PushWindow(std::deque<double>& q, double& sum, const double value) {
        q.push_back(value);
        sum += value;
        if (q.size() > cfg_.metrics_window_frames) {
            sum -= q.front();
            q.pop_front();
        }
    }

    const RuntimeConfig& cfg_;
    std::chrono::high_resolution_clock::time_point previewFpsClock_{};
    std::chrono::steady_clock::time_point lastMetricsLog_{};
    std::string overlay1_ = "Perf: warming up...";
    std::string overlay2_;
    uint64_t processedFrames_ = 0;
    uint64_t processedFramesAtLastLog_ = 0;
    uint64_t dropProxyAtLastLog_ = 0;
    std::deque<double> e2eMsWindow_;
    std::deque<double> gpuMsWindow_;
    double e2eMsSum_ = 0.0;
    double gpuMsSum_ = 0.0;
};

bool IsWorkerPaused(WorkerControlState* controlState) {
    if (!controlState) {
        return false;
    }

    std::lock_guard<std::mutex> lock(controlState->mu);
    return controlState->paused;
}

PointCloudViewConfig SnapshotPointCloudView(ViewParams* viewParams) {
    PointCloudViewConfig config;
    if (!viewParams) {
        return config;
    }

    std::lock_guard<std::mutex> lock(viewParams->mu);
    config.rotX = viewParams->rotX;
    config.rotY = viewParams->rotY;
    config.flipX = viewParams->flipX;
    config.flipY = viewParams->flipY;
    config.flipZ = viewParams->flipZ;
    config.wire = viewParams->wire;
    config.mesh = viewParams->mesh;
    return config;
}

CameraIntrinsics MakeFrameIntrinsics(const int outW, const int outH) {
    const CameraIntrinsics base =
        MakeIntrinsicsFromFovDegrees(kReferenceHfovDeg, kReferenceVfovDeg,
                                     kReferenceWidth, kReferenceHeight);
    return ScaleIntrinsics(base, outW, outH, kReferenceWidth, kReferenceHeight);
}

Mat BuildDepthColorImage(const Mat& depthMat) {
    double minVal = 0.0;
    double maxVal = 0.0;
    cv::minMaxLoc(depthMat, &minVal, &maxVal);

    Mat depthNorm;
    depthMat.convertTo(depthNorm, CV_8U, 255.0 / (maxVal - minVal + 1e-5),
                       -minVal * 255.0 / (maxVal - minVal + 1e-5));

    Mat depthColor;
    cv::applyColorMap(depthNorm, depthColor, COLORMAP_INFERNO);
    return depthColor;
}

void PublishDepthStream(DepthStreamBuffer* streamBuf,
                        const float* depthData,
                        const size_t depthCount,
                        const int outW,
                        const int outH,
                        const uint64_t frameIdx) {
    if (!streamBuf) {
        return;
    }

    std::unique_lock<std::mutex> lock(streamBuf->mu);
    streamBuf->data.assign(depthData, depthData + depthCount);
    streamBuf->width = outW;
    streamBuf->height = outH;
    streamBuf->frameIdx = static_cast<uint32_t>(frameIdx);
    streamBuf->hasFrame = true;
    lock.unlock();
    streamBuf->cv.notify_all();
}

void PublishRgbdStream(RgbdStreamBuffer* rgbdStreamBuf,
                       const float* depthData,
                       const size_t depthCount,
                       const Mat& colorForDepth,
                       const int outW,
                       const int outH,
                       const uint64_t frameIdx) {
    if (!rgbdStreamBuf) {
        return;
    }

    std::unique_lock<std::mutex> lock(rgbdStreamBuf->mu);
    rgbdStreamBuf->depth.assign(depthData, depthData + depthCount);
    if (!colorForDepth.empty() &&
        colorForDepth.type() == CV_8UC3 &&
        colorForDepth.cols == outW &&
        colorForDepth.rows == outH) {
        rgbdStreamBuf->bgr.assign(
            colorForDepth.data,
            colorForDepth.data + static_cast<size_t>(outW) * static_cast<size_t>(outH) * 3);
    } else {
        rgbdStreamBuf->bgr.assign(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 3, 0);
    }
    rgbdStreamBuf->width = outW;
    rgbdStreamBuf->height = outH;
    rgbdStreamBuf->frameIdx = static_cast<uint32_t>(frameIdx);
    rgbdStreamBuf->hasFrame = true;
    lock.unlock();
    rgbdStreamBuf->cv.notify_all();
}

Mat RenderPointCloudImage(const RuntimeConfig& cfg,
                          const Mat& depthMat,
                          const int outW,
                          const int outH,
                          const CameraIntrinsics& intrinsics,
                          const Mat& colorForDepth,
                          const PointCloudViewConfig& viewConfig,
                          const int renderWidth,
                          const int renderHeight,
                          const int renderSubsample) {
    return RenderPointCloudViewRgb(depthMat.ptr<float>(), outW, outH, intrinsics, colorForDepth,
                                   renderWidth, renderHeight, renderSubsample,
                                   cfg.point_cloud_min_depth_m, cfg.point_cloud_max_depth_m,
                                   viewConfig.rotX, viewConfig.rotY,
                                   viewConfig.flipX, viewConfig.flipY, viewConfig.flipZ,
                                   viewConfig.wire, viewConfig.mesh);
}

void PublishPointCloudStream(ImageStreamBuffer* pcStreamBuf,
                             const RuntimeConfig& cfg,
                             const Mat& depthMat,
                             const int outW,
                             const int outH,
                             const CameraIntrinsics& intrinsics,
                             const Mat& colorForDepth,
                             const PointCloudViewConfig& viewConfig,
                             const uint64_t frameIdx) {
    if (!pcStreamBuf) {
        return;
    }

    Mat pcImage = RenderPointCloudImage(cfg, depthMat, outW, outH, intrinsics, colorForDepth, viewConfig,
                                        cfg.pc_stream_render_width, cfg.pc_stream_render_height,
                                        cfg.pc_stream_render_subsample);
    std::vector<unsigned char> encoded;
    std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, cfg.png_compression};
    if (!cv::imencode(".png", pcImage, encoded, params)) {
        return;
    }

    std::unique_lock<std::mutex> lock(pcStreamBuf->mu);
    pcStreamBuf->data.swap(encoded);
    pcStreamBuf->width = pcImage.cols;
    pcStreamBuf->height = pcImage.rows;
    pcStreamBuf->frameIdx = static_cast<uint32_t>(frameIdx);
    pcStreamBuf->hasFrame = true;
    lock.unlock();
    pcStreamBuf->cv.notify_all();
}

void PublishWorkerOutputs(const RuntimeConfig& cfg,
                          DepthStreamBuffer* streamBuf,
                          RgbdStreamBuffer* rgbdStreamBuf,
                          ImageStreamBuffer* pcStreamBuf,
                          const float* depthData,
                          const size_t depthCount,
                          const Mat& depthMat,
                          const Mat& colorForDepth,
                          const int outW,
                          const int outH,
                          const uint64_t frameIdx,
                          const CameraIntrinsics& intrinsics,
                          const PointCloudViewConfig& viewConfig) {
    PublishDepthStream(streamBuf, depthData, depthCount, outW, outH, frameIdx);
    PublishRgbdStream(rgbdStreamBuf, depthData, depthCount, colorForDepth, outW, outH, frameIdx);
    PublishPointCloudStream(pcStreamBuf, cfg, depthMat, outW, outH, intrinsics, colorForDepth, viewConfig, frameIdx);
}

void RenderMainPreview(const RuntimeConfig& cfg,
                       const Mat& sourceFrame,
                       const Mat& depthColor,
                       const std::string& overlay1,
                       const std::string& overlay2,
                       const bool paused,
                       const double fps) {
    Mat showFrame;
    if (!sourceFrame.empty()) {
        cv::resize(sourceFrame, showFrame, Size(cfg.preview_width, cfg.preview_height));
    } else {
        showFrame = Mat(cfg.preview_height, cfg.preview_width, CV_8UC3, Scalar(0, 0, 0));
    }

    Mat showDepth;
    cv::resize(depthColor, showDepth, Size(cfg.preview_width, cfg.preview_height));

    Mat combined;
    cv::hconcat(showFrame, showDepth, combined);
    if (paused) {
        cv::putText(combined, "PAUSED", Point(10, 30), FONT_HERSHEY_SIMPLEX, 1,
                    Scalar(0, 255, 255), 2);
    } else {
        cv::putText(combined, "C++ CUDA FPS: " + std::to_string(static_cast<int>(fps)),
                    Point(10, 30), FONT_HERSHEY_SIMPLEX, 1, Scalar(0, 255, 0), 2);
    }
    cv::putText(combined, overlay1, Point(10, 62),
                FONT_HERSHEY_SIMPLEX, 0.55, Scalar(255, 255, 255), 1);
    if (!overlay2.empty()) {
        cv::putText(combined, overlay2, Point(10, 86),
                    FONT_HERSHEY_SIMPLEX, 0.55, Scalar(255, 255, 255), 1);
    }
    cv::imshow(kMainWindowTitle, combined);
}

void RenderPointCloudPreview(const RuntimeConfig& cfg,
                             const Mat& depthMat,
                             const int outW,
                             const int outH,
                             const CameraIntrinsics& intrinsics,
                             const Mat& colorForDepth,
                             const PointCloudViewConfig& viewConfig) {
    if (!cfg.show_point_cloud_window) {
        return;
    }

    Mat pcImage = RenderPointCloudImage(cfg, depthMat, outW, outH, intrinsics, colorForDepth, viewConfig,
                                        cfg.pc_gui_render_width, cfg.pc_gui_render_height,
                                        cfg.pc_gui_render_subsample);
    cv::imshow(kPointCloudWindowTitle, pcImage);
}

void HandlePausedState(const RuntimeConfig& cfg, const bool headless) {
    if (!headless) {
        cv::waitKey(1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.pause_loop_sleep_ms));
}

bool TryReadFrameWithReconnect(FfmpegRtspCapture& cap,
                               const RuntimeConfig& cfg,
                               const std::atomic<bool>& stopFlag,
                               const std::string& rtspUrl,
                               Mat& frame,
                               std::string& captureError,
                               int& grabFailCount,
                               uint64_t& totalReadFail,
                               uint64_t& totalReconnectSuccess,
                               uint64_t& totalReconnectFail) {
    if (cap.Read(frame, captureError)) {
        grabFailCount = 0;
        return true;
    }

    if (stopFlag.load()) {
        captureError = "stop requested";
        return false;
    }

    grabFailCount++;
    totalReadFail++;
    if (grabFailCount % cfg.grab_fail_log_every == 1) {
        LogWarn("RTSP read failed (" + std::to_string(grabFailCount) +
                "x). url=" + rtspUrl + " reason=" + captureError);
    }

    std::string reopenError;
    const auto reopenStart = std::chrono::steady_clock::now();
    if (stopFlag.load()) {
        captureError = "stop requested";
        return false;
    }
    if (cap.Reopen(reopenError)) {
        const auto reopenEnd = std::chrono::steady_clock::now();
        const auto reopenMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(reopenEnd - reopenStart).count();
        totalReconnectSuccess++;
        LogInfo("RTSP capture reopened after read failure. url=" + rtspUrl +
                " reopen_ms=" + std::to_string(reopenMs) +
                " read_fail_count=" + std::to_string(grabFailCount));
        return false;
    }

    totalReconnectFail++;
    if (grabFailCount % cfg.grab_fail_log_every == 1) {
        LogWarn("RTSP reopen failed after read failure. url=" + rtspUrl +
                " reason=" + reopenError);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.grab_retry_sleep_ms));
    return false;
}

bool PrepareInferenceInputFrame(const Mat& frame,
                                Mat& resized,
                                const uint64_t frameIdx,
                                uint64_t& totalInvalidFrame) {
    if (frame.empty()) {
        totalInvalidFrame++;
        LogWarn("Empty frame at idx=" + std::to_string(frameIdx));
        return false;
    }
    if (frame.channels() != 3) {
        totalInvalidFrame++;
        LogWarn("Unexpected channels=" + std::to_string(frame.channels()) +
                " at idx=" + std::to_string(frameIdx));
        return false;
    }

    cv::resize(frame, resized, Size(INPUT_WIDTH, INPUT_HEIGHT), 0, 0, INTER_LINEAR);
    if (resized.empty() || resized.cols != INPUT_WIDTH || resized.rows != INPUT_HEIGHT) {
        totalInvalidFrame++;
        LogWarn("Resize failed or size mismatch at idx=" + std::to_string(frameIdx));
        return false;
    }
    if (resized.type() != CV_8UC3) {
        totalInvalidFrame++;
        LogWarn("Unexpected resized type=" + std::to_string(resized.type()) +
                " at idx=" + std::to_string(frameIdx));
        return false;
    }
    return true;
}

bool ResolveDepthOutput(const TrtContextData& trt,
                        const Mat& frame,
                        std::vector<float>& outputBuffer,
                        const uint64_t frameIdx,
                        Mat& depthMat,
                        Mat& colorForDepth,
                        int& outW,
                        int& outH) {
    outH = INPUT_HEIGHT;
    outW = INPUT_WIDTH;
    ResolveOutputHW(trt.outputDims, outH, outW);
    if (static_cast<size_t>(outH) * static_cast<size_t>(outW) > trt.outputElements) {
        LogWarn("Output size mismatch outH*outW=" +
                std::to_string(static_cast<size_t>(outH) * static_cast<size_t>(outW)) +
                " > outputElements=" + std::to_string(trt.outputElements) +
                " (frame=" + std::to_string(frameIdx) + ")");
        return false;
    }

    depthMat = Mat(outH, outW, CV_32F, outputBuffer.data());
    if (frame.cols != outW || frame.rows != outH) {
        cv::resize(frame, colorForDepth, Size(outW, outH));
    } else {
        colorForDepth = frame;
    }
    return true;
}

void MaybeDumpPointCloud(const RuntimeConfig& cfg,
                         const Mat& depthMat,
                         const int outW,
                         const int outH,
                         const CameraIntrinsics& intrinsics,
                         const uint64_t frameIdx) {
    if (!cfg.dump_point_cloud || (frameIdx % cfg.point_cloud_dump_every_n != 0)) {
        return;
    }

    std::vector<cv::Vec3f> points;
    DepthToPointCloud(depthMat.ptr<float>(), outW, outH, intrinsics, points,
                      cfg.point_cloud_dump_subsample,
                      cfg.point_cloud_min_depth_m,
                      cfg.point_cloud_max_depth_m);
    const std::string plyPath = "pointcloud_" + std::to_string(frameIdx) + ".ply";
    if (SavePointCloudAsPly(plyPath, points)) {
        LogInfo("Point cloud saved: " + plyPath + " (points=" + std::to_string(points.size()) + ")");
    } else {
        LogWarn("Failed to save point cloud: " + plyPath);
    }
}
}  // namespace

// Preprocess input (BGR HWC uint8 -> RGB CHW float normalized)
__global__ void preprocess_kernel(const uint8_t* src, float* dst, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    int idx = y * width + x;

    float b = src[idx * 3 + 0] / 255.0f;
    float g = src[idx * 3 + 1] / 255.0f;
    float r = src[idx * 3 + 2] / 255.0f;

    r = (r - 0.485f) / 0.229f;
    g = (g - 0.456f) / 0.224f;
    b = (b - 0.406f) / 0.225f;

    int plane = width * height;
    dst[idx] = r;
    dst[idx + plane] = g;
    dst[idx + plane * 2] = b;
}

bool RunDepthWorker(int channel, bool headless, std::atomic<bool>& stopFlag,
                    DepthStreamBuffer* streamBuf,
                    RgbdStreamBuffer* rgbdStreamBuf,
                    ImageStreamBuffer* pcStreamBuf,
                    ViewParams* viewParams,
                    WorkerControlState* controlState,
                    WorkerStartupState* startupState) {
    const RuntimeConfig& cfg = GetRuntimeConfig();
    LogInfo("Mode: " + std::string(headless ? "headless" : "gui") +
            " | Channel: " + std::to_string(channel));

    bool startupReported = false;
    const auto reportStartupFailure = [&](const std::string& detail) {
        if (!startupReported) {
            CompleteWorkerStartup(startupState, false, detail);
            startupReported = true;
        }
    };
    const auto reportStartupSuccess = [&]() {
        if (!startupReported) {
            CompleteWorkerStartup(startupState, true, "");
            startupReported = true;
        }
    };

    TrtContextData trt;
    if (!InitTrt(trt)) {
        reportStartupFailure("TensorRT init failed");
        return false;
    }

    CudaResources cudaRes;
    if (!InitCudaResources(trt, cudaRes)) {
        reportStartupFailure("CUDA resource init failed");
        return false;
    }

    const std::string& rtspUrl = RTSP_URLS[channel];
    FfmpegRtspCapture cap;
    cap.SetInterruptStopFlag(&stopFlag);
    std::string captureError;
    if (!cap.Open(rtspUrl, cfg, captureError)) {
        reportStartupFailure("RTSP open failed: " + captureError);
        LogError("Cannot open RTSP stream: " + rtspUrl + " reason=" + captureError);
        return false;
    }

    Mat frame, resized;
    std::vector<float> outputBuffer(trt.outputElements);
    uint64_t frameIdx = 0;

    LogInfo("Stream started.");

    dim3 block(16, 16);
    dim3 grid((INPUT_WIDTH + block.x - 1) / block.x, (INPUT_HEIGHT + block.y - 1) / block.y);

    WorkerPerfTracker perfTracker(cfg);

    int grabFailCount = 0;
    uint64_t totalReadFail = 0;
    uint64_t totalInvalidFrame = 0;
    uint64_t totalReconnectSuccess = 0;
    uint64_t totalReconnectFail = 0;

    bool fatalError = false;
    std::string fatalStartupDetail;
    while (!stopFlag.load()) {
        if (IsWorkerPaused(controlState)) {
            HandlePausedState(cfg, headless);
            continue;
        }

        if (!TryReadFrameWithReconnect(cap, cfg, stopFlag, rtspUrl, frame, captureError,
                                       grabFailCount, totalReadFail,
                                       totalReconnectSuccess, totalReconnectFail)) {
            continue;
        }

        frameIdx++;
        const auto frameStart = std::chrono::steady_clock::now();
        if (!PrepareInferenceInputFrame(frame, resized, frameIdx, totalInvalidFrame)) {
            continue;
        }

        const auto gpuStart = std::chrono::steady_clock::now();
        if (!resized.isContinuous()) {
            resized = resized.clone();
        }
        if (!CheckCuda(cudaMemcpyAsync(cudaRes.d_raw_img, resized.data, trt.rawInputSize,
                                        cudaMemcpyHostToDevice, cudaRes.stream),
                       "cudaMemcpyAsync(d_raw_img)", __FILE__, __LINE__)) {
            fatalError = true;
            fatalStartupDetail = "CUDA input upload failed";
            break;
        }

        preprocess_kernel<<<grid, block, 0, cudaRes.stream>>>((uint8_t*)cudaRes.d_raw_img,
                                                               (float*)cudaRes.d_input,
                                                               INPUT_WIDTH, INPUT_HEIGHT);
        auto err = cudaGetLastError();
        if (err != cudaSuccess) {
            LogError(std::string("CUDA kernel error: ") + cudaGetErrorString(err) +
                     " (frame=" + std::to_string(frameIdx) + ")");
            fatalError = true;
            fatalStartupDetail = "CUDA preprocess kernel failed: " + std::string(cudaGetErrorString(err));
            break;
        }

        if (!trt.context->enqueueV3(cudaRes.stream)) {
            LogError("enqueueV3 failed (frame=" + std::to_string(frameIdx) + ")");
            fatalError = true;
            fatalStartupDetail = "TensorRT enqueue failed";
            break;
        }
        err = cudaGetLastError();
        if (err != cudaSuccess) {
            LogError(std::string("CUDA post-enqueue error: ") + cudaGetErrorString(err) +
                     " (frame=" + std::to_string(frameIdx) + ")");
            fatalError = true;
            fatalStartupDetail = "CUDA post-enqueue failed: " + std::string(cudaGetErrorString(err));
            break;
        }

        if (!CheckCuda(cudaMemcpyAsync(outputBuffer.data(), cudaRes.d_output, trt.outputSize,
                                        cudaMemcpyDeviceToHost, cudaRes.stream),
                       "cudaMemcpyAsync(output)", __FILE__, __LINE__)) {
            fatalError = true;
            fatalStartupDetail = "CUDA output download failed";
            break;
        }
        err = cudaStreamSynchronize(cudaRes.stream);
        if (err != cudaSuccess) {
            LogError(std::string("CUDA stream sync error: ") + cudaGetErrorString(err) +
                     " (frame=" + std::to_string(frameIdx) + ")");
            fatalError = true;
            fatalStartupDetail = "CUDA stream sync failed: " + std::string(cudaGetErrorString(err));
            break;
        }
        const auto gpuEnd = std::chrono::steady_clock::now();

        int outW = INPUT_WIDTH;
        int outH = INPUT_HEIGHT;
        Mat depthMat;
        Mat colorForDepth;
        if (!ResolveDepthOutput(trt, frame, outputBuffer, frameIdx,
                                depthMat, colorForDepth, outW, outH)) {
            continue;
        }
        const CameraIntrinsics intrinsics = MakeFrameIntrinsics(outW, outH);
        MaybeDumpPointCloud(cfg, depthMat, outW, outH, intrinsics, frameIdx);

        const Mat depthColor = BuildDepthColorImage(depthMat);
        const PointCloudViewConfig viewConfig = SnapshotPointCloudView(viewParams);

        PublishWorkerOutputs(cfg, streamBuf, rgbdStreamBuf, pcStreamBuf,
                             outputBuffer.data(), outputBuffer.size(),
                             depthMat, colorForDepth,
                             outW, outH, frameIdx,
                             intrinsics, viewConfig);

        if (!headless) {
            RenderMainPreview(cfg, frame, depthColor,
                              perfTracker.Overlay1(), perfTracker.Overlay2(),
                              false, perfTracker.TickPreviewFps());
            RenderPointCloudPreview(cfg, depthMat, outW, outH, intrinsics, colorForDepth, viewConfig);
            cv::waitKey(1);
        }
        reportStartupSuccess();

        const auto frameEnd = std::chrono::steady_clock::now();
        const double gpuMs =
            std::chrono::duration<double, std::milli>(gpuEnd - gpuStart).count();
        const double e2eMs =
            std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        perfTracker.RecordFrame(e2eMs, gpuMs);
        perfTracker.MaybeEmit(headless, totalReadFail, totalInvalidFrame,
                              totalReconnectSuccess, totalReconnectFail);
    }

    cap.Close();
    if (!headless) {
        cv::destroyAllWindows();
    }

    if (!startupReported) {
        if (fatalError) {
            CompleteWorkerStartup(startupState, false,
                                  fatalStartupDetail.empty()
                                      ? "Worker exited before startup completed"
                                      : fatalStartupDetail);
        } else {
            CompleteWorkerStartup(startupState, false, "Worker stopped before startup completed");
        }
    }

    if (fatalError) return false;
    return true;
}

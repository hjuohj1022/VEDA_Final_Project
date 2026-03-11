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
                    ViewParams* viewParams) {
    const RuntimeConfig& cfg = GetRuntimeConfig();
    LogInfo("Mode: " + std::string(headless ? "headless" : "gui") +
            " | Channel: " + std::to_string(channel));

    TrtContextData trt;
    if (!InitTrt(trt)) return false;

    CudaResources cudaRes;
    if (!InitCudaResources(trt, cudaRes)) return false;

    const std::string& rtspUrl = RTSP_URLS[channel];
    FfmpegRtspCapture cap;
    std::string captureError;
    if (!cap.Open(rtspUrl, cfg, captureError)) {
        LogError("Cannot open RTSP stream: " + rtspUrl + " reason=" + captureError);
        return false;
    }

    Mat frame, resized;
    std::vector<float> outputBuffer(trt.outputElements);
    uint64_t frameIdx = 0;
    Mat frozenDepth;
    Mat frozenColor;
    Mat frozenFrame;
    int frozenW = 0;
    int frozenH = 0;
    uint64_t frozenFrameIdx = 0;

    LogInfo("Stream started.");

    dim3 block(16, 16);
    dim3 grid((INPUT_WIDTH + block.x - 1) / block.x, (INPUT_HEIGHT + block.y - 1) / block.y);

    auto prevTime = std::chrono::high_resolution_clock::now();
    auto lastMetricsLog = std::chrono::steady_clock::now();
    std::string perfOverlay1 = "Perf: warming up...";
    std::string perfOverlay2 = "";

    int grabFailCount = 0;
    uint64_t totalReadFail = 0;
    uint64_t totalInvalidFrame = 0;
    uint64_t processedFrames = 0;
    uint64_t processedFramesAtLastLog = 0;
    uint64_t dropProxyAtLastLog = 0;
    std::deque<double> e2eMsWindow;
    std::deque<double> gpuMsWindow;
    double e2eMsSum = 0.0;
    double gpuMsSum = 0.0;
    auto pushWindow = [&](std::deque<double>& q, double& sum, double v) {
        q.push_back(v);
        sum += v;
        if (q.size() > cfg.metrics_window_frames) {
            sum -= q.front();
            q.pop_front();
        }
    };

    bool fatalError = false;
    while (!stopFlag.load()) {
        bool paused = false;
        if (viewParams) {
            std::lock_guard<std::mutex> lock(viewParams->mu);
            paused = viewParams->paused;
        }
        if (paused) {
            if (frozenDepth.empty() || frozenW <= 0 || frozenH <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(cfg.pause_loop_sleep_ms));
                continue;
            }

            const int outW = frozenW;
            const int outH = frozenH;
            Mat depthMat = frozenDepth;
            Mat colorForDepth = frozenColor;
            if (colorForDepth.empty()) {
                colorForDepth = Mat(outH, outW, CV_8UC3, Scalar(0, 0, 0));
            }

            double minVal, maxVal;
            cv::minMaxLoc(depthMat, &minVal, &maxVal);
            Mat depthNorm;
            depthMat.convertTo(depthNorm, CV_8U, 255.0 / (maxVal - minVal + 1e-5),
                               -minVal * 255.0 / (maxVal - minVal + 1e-5));
            Mat depthColor;
            cv::applyColorMap(depthNorm, depthColor, COLORMAP_INFERNO);

            const uint64_t frozenIdx = frozenFrameIdx;
            if (streamBuf) {
                std::unique_lock<std::mutex> lock(streamBuf->mu);
                const size_t count = static_cast<size_t>(outW) * static_cast<size_t>(outH);
                streamBuf->data.assign(depthMat.ptr<float>(), depthMat.ptr<float>() + count);
                streamBuf->width = outW;
                streamBuf->height = outH;
                streamBuf->frameIdx = static_cast<uint32_t>(frozenIdx);
                streamBuf->hasFrame = true;
                lock.unlock();
                streamBuf->cv.notify_all();
            }

            if (rgbdStreamBuf) {
                std::unique_lock<std::mutex> lock(rgbdStreamBuf->mu);
                const size_t count = static_cast<size_t>(outW) * static_cast<size_t>(outH);
                rgbdStreamBuf->depth.assign(depthMat.ptr<float>(), depthMat.ptr<float>() + count);
                if (!colorForDepth.empty() && colorForDepth.type() == CV_8UC3 &&
                    colorForDepth.cols == outW && colorForDepth.rows == outH) {
                    rgbdStreamBuf->bgr.assign(colorForDepth.data,
                                              colorForDepth.data + static_cast<size_t>(outW) * static_cast<size_t>(outH) * 3);
                } else {
                    rgbdStreamBuf->bgr.assign(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 3, 0);
                }
                rgbdStreamBuf->width = outW;
                rgbdStreamBuf->height = outH;
                rgbdStreamBuf->frameIdx = static_cast<uint32_t>(frozenIdx);
                rgbdStreamBuf->hasFrame = true;
                lock.unlock();
                rgbdStreamBuf->cv.notify_all();
            }

            if (pcStreamBuf) {
                float rx = -20.0f;
                float ry = 35.0f;
                bool flipX = false;
                bool flipY = false;
                bool flipZ = false;
                bool wire = false;
                bool mesh = false;
                if (viewParams) {
                    std::lock_guard<std::mutex> lock(viewParams->mu);
                    rx = viewParams->rotX;
                    ry = viewParams->rotY;
                    flipX = viewParams->flipX;
                    flipY = viewParams->flipY;
                    flipZ = viewParams->flipZ;
                    wire = viewParams->wire;
                    mesh = viewParams->mesh;
                }
                const CameraIntrinsics Kfhd = MakeIntrinsicsFromFovDegrees(109.0f, 55.0f, 1920, 1080);
                const CameraIntrinsics K = ScaleIntrinsics(Kfhd, outW, outH, 1920, 1080);
                Mat pcv = RenderPointCloudViewRgb(depthMat.ptr<float>(), outW, outH, K, colorForDepth,
                                                  cfg.pc_stream_render_width, cfg.pc_stream_render_height,
                                                  cfg.pc_stream_render_subsample,
                                                  cfg.point_cloud_min_depth_m, cfg.point_cloud_max_depth_m, rx, ry,
                                                  flipX, flipY, flipZ, wire, mesh);
                std::vector<unsigned char> encoded;
                std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, cfg.png_compression};
                if (cv::imencode(".png", pcv, encoded, params)) {
                    std::unique_lock<std::mutex> lock(pcStreamBuf->mu);
                    pcStreamBuf->data.swap(encoded);
                    pcStreamBuf->width = pcv.cols;
                    pcStreamBuf->height = pcv.rows;
                    pcStreamBuf->frameIdx = static_cast<uint32_t>(frozenIdx);
                    pcStreamBuf->hasFrame = true;
                    lock.unlock();
                    pcStreamBuf->cv.notify_all();
                }
            }

            if (!headless) {
                Mat showFrame, showDepth;
                if (!frozenFrame.empty()) {
                    cv::resize(frozenFrame, showFrame, Size(cfg.preview_width, cfg.preview_height));
                } else {
                    showFrame = Mat(cfg.preview_height, cfg.preview_width, CV_8UC3, Scalar(0, 0, 0));
                }
                cv::resize(depthColor, showDepth, Size(cfg.preview_width, cfg.preview_height));
                Mat combined;
                cv::hconcat(showFrame, showDepth, combined);
                cv::putText(combined, "PAUSED", Point(10, 30), FONT_HERSHEY_SIMPLEX, 1, Scalar(0, 255, 255), 2);
                cv::putText(combined, perfOverlay1, Point(10, 62),
                            FONT_HERSHEY_SIMPLEX, 0.55, Scalar(255, 255, 255), 1);
                if (!perfOverlay2.empty()) {
                    cv::putText(combined, perfOverlay2, Point(10, 86),
                                FONT_HERSHEY_SIMPLEX, 0.55, Scalar(255, 255, 255), 1);
                }
                cv::imshow("Depth Anything 3 Metric (CUDA Optimized)", combined);
                cv::waitKey(1);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.pause_loop_sleep_ms));
            continue;
        }

        if (!cap.Read(frame, captureError)) {
            grabFailCount++;
            totalReadFail++;
            if (grabFailCount % cfg.grab_fail_log_every == 1) {
                LogWarn("RTSP read failed (" + std::to_string(grabFailCount) +
                        "x). url=" + rtspUrl + " reason=" + captureError);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.grab_retry_sleep_ms));
            continue;
        }
        grabFailCount = 0;
        frameIdx++;
        const auto frameStart = std::chrono::steady_clock::now();
        if (frame.empty()) {
            totalInvalidFrame++;
            LogWarn("Empty frame at idx=" + std::to_string(frameIdx));
            continue;
        }
        if (frame.channels() != 3) {
            totalInvalidFrame++;
            LogWarn("Unexpected channels=" + std::to_string(frame.channels()) +
                    " at idx=" + std::to_string(frameIdx));
            continue;
        }

        cv::resize(frame, resized, Size(INPUT_WIDTH, INPUT_HEIGHT), 0, 0, INTER_LINEAR);
        if (resized.empty() || resized.cols != INPUT_WIDTH || resized.rows != INPUT_HEIGHT) {
            totalInvalidFrame++;
            LogWarn("Resize failed or size mismatch at idx=" + std::to_string(frameIdx));
            continue;
        }
        if (resized.type() != CV_8UC3) {
            totalInvalidFrame++;
            LogWarn("Unexpected resized type=" + std::to_string(resized.type()) +
                    " at idx=" + std::to_string(frameIdx));
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
            break;
        }

        if (!trt.context->enqueueV3(cudaRes.stream)) {
            LogError("enqueueV3 failed (frame=" + std::to_string(frameIdx) + ")");
            fatalError = true;
            break;
        }
        err = cudaGetLastError();
        if (err != cudaSuccess) {
            LogError(std::string("CUDA post-enqueue error: ") + cudaGetErrorString(err) +
                     " (frame=" + std::to_string(frameIdx) + ")");
            fatalError = true;
            break;
        }

        if (!CheckCuda(cudaMemcpyAsync(outputBuffer.data(), cudaRes.d_output, trt.outputSize,
                                        cudaMemcpyDeviceToHost, cudaRes.stream),
                       "cudaMemcpyAsync(output)", __FILE__, __LINE__)) {
            fatalError = true;
            break;
        }
        err = cudaStreamSynchronize(cudaRes.stream);
        if (err != cudaSuccess) {
            LogError(std::string("CUDA stream sync error: ") + cudaGetErrorString(err) +
                     " (frame=" + std::to_string(frameIdx) + ")");
            fatalError = true;
            break;
        }
        const auto gpuEnd = std::chrono::steady_clock::now();

        int outH = INPUT_HEIGHT;
        int outW = INPUT_WIDTH;
        ResolveOutputHW(trt.outputDims, outH, outW);
        if (static_cast<size_t>(outH) * static_cast<size_t>(outW) > trt.outputElements) {
            LogWarn("Output size mismatch outH*outW=" +
                    std::to_string(static_cast<size_t>(outH) * static_cast<size_t>(outW)) +
                    " > outputElements=" + std::to_string(trt.outputElements) +
                    " (frame=" + std::to_string(frameIdx) + ")");
            continue;
        }
        Mat depthMat(outH, outW, CV_32F, outputBuffer.data());
        Mat colorForDepth;
        if (frame.cols != outW || frame.rows != outH) {
            cv::resize(frame, colorForDepth, Size(outW, outH));
        } else {
            colorForDepth = frame;
        }
        double minVal, maxVal;
        cv::minMaxLoc(depthMat, &minVal, &maxVal);

        const bool dumpPointCloud = cfg.dump_point_cloud;
        const bool showPointCloud = cfg.show_point_cloud_window;
        const CameraIntrinsics Kfhd = MakeIntrinsicsFromFovDegrees(109.0f, 55.0f, 1920, 1080);
        const CameraIntrinsics K = ScaleIntrinsics(Kfhd, outW, outH, 1920, 1080);
        if (dumpPointCloud && (frameIdx % cfg.point_cloud_dump_every_n == 0)) {
            std::vector<cv::Vec3f> points;
            DepthToPointCloud(depthMat.ptr<float>(), outW, outH, K, points,
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

        Mat depthNorm;
        depthMat.convertTo(depthNorm, CV_8U, 255.0 / (maxVal - minVal + 1e-5),
                           -minVal * 255.0 / (maxVal - minVal + 1e-5));

        Mat depthColor;
        cv::applyColorMap(depthNorm, depthColor, COLORMAP_INFERNO);

        Mat showFrame, showDepth;
        cv::resize(frame, showFrame, Size(cfg.preview_width, cfg.preview_height));
        cv::resize(depthColor, showDepth, Size(cfg.preview_width, cfg.preview_height));

        if (streamBuf) {
            std::unique_lock<std::mutex> lock(streamBuf->mu);
            streamBuf->data.assign(outputBuffer.begin(), outputBuffer.end());
            streamBuf->width = outW;
            streamBuf->height = outH;
            streamBuf->frameIdx = static_cast<uint32_t>(frameIdx);
            streamBuf->hasFrame = true;
            lock.unlock();
            streamBuf->cv.notify_all();
        }

        if (rgbdStreamBuf) {
            std::unique_lock<std::mutex> lock(rgbdStreamBuf->mu);
            rgbdStreamBuf->depth.assign(outputBuffer.begin(), outputBuffer.end());
            if (!colorForDepth.empty() && colorForDepth.type() == CV_8UC3 &&
                colorForDepth.cols == outW && colorForDepth.rows == outH) {
                rgbdStreamBuf->bgr.assign(colorForDepth.data,
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

        if (pcStreamBuf) {
            float rx = -20.0f;
            float ry = 35.0f;
            bool flipX = false;
            bool flipY = false;
            bool flipZ = false;
            bool wire = false;
            bool mesh = false;
            if (viewParams) {
                std::lock_guard<std::mutex> lock(viewParams->mu);
                rx = viewParams->rotX;
                ry = viewParams->rotY;
                flipX = viewParams->flipX;
                flipY = viewParams->flipY;
                flipZ = viewParams->flipZ;
                wire = viewParams->wire;
                mesh = viewParams->mesh;
            }
            Mat pcv = RenderPointCloudViewRgb(depthMat.ptr<float>(), outW, outH, K, colorForDepth,
                                              cfg.pc_stream_render_width, cfg.pc_stream_render_height,
                                              cfg.pc_stream_render_subsample,
                                              cfg.point_cloud_min_depth_m, cfg.point_cloud_max_depth_m,
                                              rx, ry, flipX, flipY, flipZ, wire, mesh);
            std::vector<unsigned char> encoded;
            std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, cfg.png_compression};
            if (cv::imencode(".png", pcv, encoded, params)) {
                std::unique_lock<std::mutex> lock(pcStreamBuf->mu);
                pcStreamBuf->data.swap(encoded);
                pcStreamBuf->width = pcv.cols;
                pcStreamBuf->height = pcv.rows;
                pcStreamBuf->frameIdx = static_cast<uint32_t>(frameIdx);
                pcStreamBuf->hasFrame = true;
                lock.unlock();
                pcStreamBuf->cv.notify_all();
            }
        }

        if (!headless) {
            Mat combined;
            cv::hconcat(showFrame, showDepth, combined);

            auto currTime = std::chrono::high_resolution_clock::now();
            double fps = 1.0 / std::chrono::duration<double>(currTime - prevTime).count();
            prevTime = currTime;

            cv::putText(combined, "C++ CUDA FPS: " + std::to_string((int)fps),
                        Point(10, 30), FONT_HERSHEY_SIMPLEX, 1, Scalar(0, 255, 0), 2);
            cv::putText(combined, perfOverlay1, Point(10, 62),
                        FONT_HERSHEY_SIMPLEX, 0.55, Scalar(255, 255, 255), 1);
            if (!perfOverlay2.empty()) {
                cv::putText(combined, perfOverlay2, Point(10, 86),
                            FONT_HERSHEY_SIMPLEX, 0.55, Scalar(255, 255, 255), 1);
            }
            cv::imshow("Depth Anything 3 Metric (CUDA Optimized)", combined);
            if (showPointCloud) {
                float rx = -20.0f;
                float ry = 35.0f;
                bool flipX = false;
                bool flipY = false;
                bool flipZ = false;
                bool wire = false;
                bool mesh = false;
                if (viewParams) {
                    std::lock_guard<std::mutex> lock(viewParams->mu);
                    rx = viewParams->rotX;
                    ry = viewParams->rotY;
                    flipX = viewParams->flipX;
                    flipY = viewParams->flipY;
                    flipZ = viewParams->flipZ;
                    wire = viewParams->wire;
                    mesh = viewParams->mesh;
                }
                Mat pcv = RenderPointCloudViewRgb(depthMat.ptr<float>(), outW, outH, K, colorForDepth,
                                                  cfg.pc_gui_render_width, cfg.pc_gui_render_height,
                                                  cfg.pc_gui_render_subsample,
                                                  cfg.point_cloud_min_depth_m, cfg.point_cloud_max_depth_m,
                                                  rx, ry, flipX, flipY, flipZ, wire, mesh);
                cv::imshow("PointCloud (Projected)", pcv);
            }
            cv::waitKey(1);
        }

        frozenDepth = depthMat.clone();
        frozenColor = colorForDepth.clone();
        frozenFrame = frame.clone();
        frozenW = outW;
        frozenH = outH;
        frozenFrameIdx = frameIdx;

        const auto frameEnd = std::chrono::steady_clock::now();
        const double gpuMs =
            std::chrono::duration<double, std::milli>(gpuEnd - gpuStart).count();
        const double e2eMs =
            std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        pushWindow(gpuMsWindow, gpuMsSum, gpuMs);
        pushWindow(e2eMsWindow, e2eMsSum, e2eMs);
        processedFrames++;

        const auto now = std::chrono::steady_clock::now();
        const double logIntervalSec = std::chrono::duration<double>(now - lastMetricsLog).count();
        if (logIntervalSec >= cfg.metrics_log_interval_sec) {
            const uint64_t intervalProcessed = processedFrames - processedFramesAtLastLog;
            const uint64_t dropProxyNow = totalReadFail + totalInvalidFrame;
            const uint64_t intervalDropProxy = dropProxyNow - dropProxyAtLastLog;
            const double procFps = intervalProcessed / logIntervalSec;
            const double avgE2eMs = e2eMsWindow.empty() ? 0.0 : (e2eMsSum / static_cast<double>(e2eMsWindow.size()));
            const double avgGpuMs = gpuMsWindow.empty() ? 0.0 : (gpuMsSum / static_cast<double>(gpuMsWindow.size()));
            const double dropProxyRatio = (intervalProcessed + intervalDropProxy) == 0
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
                 << ", invalid=" << totalInvalidFrame << ")";
            if (headless) {
                LogInfo("[Perf] " + oss1.str() + " " + oss2.str());
            } else {
                perfOverlay1 = oss1.str();
                perfOverlay2 = oss2.str();
            }

            lastMetricsLog = now;
            processedFramesAtLastLog = processedFrames;
            dropProxyAtLastLog = dropProxyNow;
        }
    }

    cap.Close();
    if (!headless) {
        cv::destroyAllWindows();
    }

    if (fatalError) return false;
    return true;
}

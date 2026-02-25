#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <thread>

#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <opencv2/opencv.hpp>

using namespace nvinfer1;
using namespace cv;

// 설정 파일 포함 (.gitignore로 관리됨)
#include "app_config.h"

#define checkCudaErrors(val) check((val), #val, __FILE__, __LINE__)
void check(cudaError_t result, char const* const func, const char* const file, int const line) {
    if (result) {
        std::cerr << "CUDA error at " << file << ":" << line << " code=" << static_cast<int>(result) << " \"" << func << "\" \n";
        exit(EXIT_FAILURE);
    }
}

class Logger : public ILogger {
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING) std::cout << "[TRT] " << msg << std::endl;
    }
} gLogger;

int main() {
    std::cout << "🚀 Loading Engine (Release Optimized)..." << std::endl;
    std::ifstream file(ENGINE_PATH, std::ios::binary | std::ios::ate);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> engineData(size);
    file.read(engineData.data(), size);
    file.close();

    std::unique_ptr<IRuntime> runtime{createInferRuntime(gLogger)};
    std::unique_ptr<ICudaEngine> engine{runtime->deserializeCudaEngine(engineData.data(), size)};
    std::unique_ptr<IExecutionContext> context{engine->createExecutionContext()};

    size_t inputSize = 1 * 3 * INPUT_SIZE * INPUT_SIZE * sizeof(float);
    size_t outputSize = 1 * INPUT_SIZE * INPUT_SIZE * sizeof(float);

    void *d_input, *d_output;
    checkCudaErrors(cudaMalloc(&d_input, inputSize));
    checkCudaErrors(cudaMalloc(&d_output, outputSize));

    // TensorRT 10.x 이상에서는 텐서 이름을 직접 조회해서 바인딩해야 안전합니다.
    // 엔진 내부의 실제 이름(예: "input", "images", "depth_head" 등)과 다르면 바인딩이 안 되어
    // enqueueV3 실행 시 GPU가 잘못된 주소를 참조하여 드라이버가 뻗을 수 있습니다.
    for (int i = 0; i < engine->getNbIOTensors(); ++i) {
        const char* name = engine->getIOTensorName(i);
        TensorIOMode mode = engine->getTensorIOMode(name);
        if (mode == TensorIOMode::kINPUT) {
            std::cout << "[TRT] Found Input Tensor: " << name << std::endl;
            context->setTensorAddress(name, d_input);
        } else if (mode == TensorIOMode::kOUTPUT) {
            std::cout << "[TRT] Found Output Tensor: " << name << std::endl;
            context->setTensorAddress(name, d_output);
        }
    }

    _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp");
    VideoCapture cap(GetSelectedRtspUrl(), CAP_FFMPEG);
    cap.set(CAP_PROP_BUFFERSIZE, 1);

    Mat frame, resized, floatImg;
    std::vector<float> inputBuffer(3 * INPUT_SIZE * INPUT_SIZE);
    std::vector<float> outputBuffer(INPUT_SIZE * INPUT_SIZE);

    // 정규화 파라미터 미리 설정
    Scalar mean(0.485, 0.456, 0.406);
    Scalar std(0.229, 0.224, 0.225);

    auto prevTime = std::chrono::high_resolution_clock::now();

    while (true) {
        if (!cap.grab()) {
            std::cout << "[WARN] No Frame... wait..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (!cap.retrieve(frame)) continue;

        // --------------------------------------
        // 초고속 전처리 (벡터 연산)
        // --------------------------------------
        cv::resize(frame, resized, Size(INPUT_SIZE, INPUT_SIZE), 0, 0, INTER_LINEAR);
        cv::cvtColor(resized, resized, COLOR_BGR2RGB);
        resized.convertTo(floatImg, CV_32FC3, 1.0 / 255.0);
        
        // (val - mean) / std (OpenCV 매트릭스 연산은 루프보다 수십 배 빠릅니다)
        floatImg = (floatImg - mean) / std;

        // HWC -> CHW (Plane 분리)
        std::vector<Mat> channels(3);
        cv::split(floatImg, channels);
        for (int i = 0; i < 3; ++i) {
            std::memcpy(inputBuffer.data() + i * INPUT_SIZE * INPUT_SIZE, channels[i].data, INPUT_SIZE * INPUT_SIZE * sizeof(float));
        }

        checkCudaErrors(cudaMemcpy(d_input, inputBuffer.data(), inputSize, cudaMemcpyHostToDevice));

        context->enqueueV3(0);

        checkCudaErrors(cudaMemcpy(outputBuffer.data(), d_output, outputSize, cudaMemcpyDeviceToHost));
        
        // --------------------------------------
        // 시각화
        // --------------------------------------
        Mat depthMat(INPUT_SIZE, INPUT_SIZE, CV_32F, outputBuffer.data());
        double minV, maxV;
        cv::minMaxLoc(depthMat, &minV, &maxV);
        Mat depthNorm;
        depthMat.convertTo(depthNorm, CV_8U, 255.0 / (maxV - minV + 1e-5), -minV * 255.0 / (maxV - minV + 1e-5));

        Mat depthColor;
        cv::applyColorMap(depthNorm, depthColor, COLORMAP_INFERNO);
        
        Mat showF, showD;
        cv::resize(frame, showF, Size(640, 480));
        cv::resize(depthColor, showD, Size(640, 480));
        Mat combined;
        cv::hconcat(showF, showD, combined);

        auto currTime = std::chrono::high_resolution_clock::now();
        double fps = 1.0 / std::chrono::duration<double>(currTime - prevTime).count();
        prevTime = currTime;

        cv::putText(combined, "C++ RELEASE FPS: " + std::to_string((int)fps), Point(10, 30), FONT_HERSHEY_SIMPLEX, 1, Scalar(0, 255, 0), 2);
        cv::imshow("Depth Anything V2 (C++ Release)", combined);

        if (cv::waitKey(1) == 'q') break;
    }

    cudaFree(d_input);
    cudaFree(d_output);
    return 0;
}

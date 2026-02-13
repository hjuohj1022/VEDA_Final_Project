#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <thread>

// TensorRT & CUDA
#include <NvInfer.h>
#include <cuda_runtime_api.h>

// OpenCV
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

// ==========================================
// [CUDA 커널] 전처리 (Resize + Normalize + CHW)
// OpenCV CPU Resize는 느리므로, 원본 그대로 GPU로 올려서 처리하는게 가장 빠름.
// 하지만 구현 복잡도를 낮추기 위해 CPU에서 Resize 후 GPU로 올린 뒤
// "Normalization + HWC->CHW" 변환을 GPU 커널로 처리.
// ==========================================
__global__ void preprocess_kernel(const uint8_t* src, float* dst, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    int idx = y * width + x;
    
    // BGR -> RGB 순서로 접근 및 Normalize
    // src는 HWC(BGR), dst는 CHW(RGB)
    
    float b = src[idx * 3 + 0] / 255.0f;
    float g = src[idx * 3 + 1] / 255.0f;
    float r = src[idx * 3 + 2] / 255.0f;

    // (val - mean) / std (DepthAnything V2 Standard)
    r = (r - 0.485f) / 0.229f;
    g = (g - 0.456f) / 0.224f;
    b = (b - 0.406f) / 0.225f;

    // Plane offset (Channel size)
    int plane = width * height;

    // CHW 저장 (RGB 순서)
    dst[idx]             = r; // 0번 채널
    dst[idx + plane]     = g; // 1번 채널
    dst[idx + plane * 2] = b; // 2번 채널
}

class Logger : public ILogger {
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING) std::cout << "[TRT] " << msg << std::endl;
    }
} gLogger;

int main() {
    // --------------------------------------
    // 1. TensorRT 초기화
    // --------------------------------------
    std::cout << "🚀 Loading Engine (CUDA Optimized Version)..." << std::endl;
    std::ifstream file(ENGINE_PATH, std::ios::binary | std::ios::ate);
    if (!file.good()) {
        std::cerr << "Error: Cannot find engine file at " << ENGINE_PATH << std::endl;
        return -1;
    }
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> engineData(size);
    file.read(engineData.data(), size);
    file.close();

    std::unique_ptr<IRuntime> runtime{createInferRuntime(gLogger)};
    std::unique_ptr<ICudaEngine> engine{runtime->deserializeCudaEngine(engineData.data(), size)};
    std::unique_ptr<IExecutionContext> context{engine->createExecutionContext()};

    // --------------------------------------
    // 2. 메모리 할당
    // --------------------------------------
    size_t inputSize = 1 * 3 * INPUT_SIZE * INPUT_SIZE * sizeof(float);
    size_t outputSize = 1 * INPUT_SIZE * INPUT_SIZE * sizeof(float);
    size_t rawInputSize = INPUT_SIZE * INPUT_SIZE * 3 * sizeof(uint8_t); 

    void *d_input, *d_output, *d_raw_img;
    checkCudaErrors(cudaMalloc(&d_input, inputSize));
    checkCudaErrors(cudaMalloc(&d_output, outputSize));
    checkCudaErrors(cudaMalloc(&d_raw_img, rawInputSize)); 

    // 동적 텐서 바인딩 (안전함)
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

    // --------------------------------------
    // 3. 스트리밍 시작
    // --------------------------------------
    _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp");
    VideoCapture cap(RTSP_URL, CAP_FFMPEG);
    cap.set(CAP_PROP_BUFFERSIZE, 1);
    if (!cap.isOpened()) {
        std::cerr << "Error: Cannot open RTSP stream" << std::endl;
        return -1;
    }
    
    Mat frame, resized;
    std::vector<float> outputBuffer(INPUT_SIZE * INPUT_SIZE);

    std::cout << "Stream Started. Press 'q' to exit." << std::endl;
    
    // CUDA Grid/Block 설정
    dim3 block(16, 16);
    dim3 grid((INPUT_SIZE + block.x - 1) / block.x, (INPUT_SIZE + block.y - 1) / block.y);

    auto prevTime = std::chrono::high_resolution_clock::now();

    while (true) {
        if (!cap.grab()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (!cap.retrieve(frame)) continue;

        // 1. CPU Resize (INTER_LINEAR)
        cv::resize(frame, resized, Size(INPUT_SIZE, INPUT_SIZE), 0, 0, INTER_LINEAR);

        // 2. Raw Image Copy (CPU -> GPU)
        checkCudaErrors(cudaMemcpy(d_raw_img, resized.data, rawInputSize, cudaMemcpyHostToDevice));

        // 3. CUDA Preprocess Kernel 실행 (Normalize & HWC->CHW)
        preprocess_kernel<<<grid, block>>>((uint8_t*)d_raw_img, (float*)d_input, INPUT_SIZE, INPUT_SIZE);

        // 4. TensorRT 추론
        context->enqueueV3(0);

        // 5. 결과 회수 (GPU -> CPU)
        checkCudaErrors(cudaMemcpy(outputBuffer.data(), d_output, outputSize, cudaMemcpyDeviceToHost));
        
        // 6. 시각화 (CPU)
        Mat depthMat(INPUT_SIZE, INPUT_SIZE, CV_32F, outputBuffer.data());
        double minVal, maxVal;
        cv::minMaxLoc(depthMat, &minVal, &maxVal);
        
        Mat depthNorm;
        depthMat.convertTo(depthNorm, CV_8U, 255.0 / (maxVal - minVal + 1e-5), -minVal * 255.0 / (maxVal - minVal + 1e-5));

        Mat depthColor;
        cv::applyColorMap(depthNorm, depthColor, COLORMAP_INFERNO);
        
        Mat showFrame, showDepth;
        cv::resize(frame, showFrame, Size(640, 480));
        cv::resize(depthColor, showDepth, Size(640, 480));
        
        Mat combined;
        cv::hconcat(showFrame, showDepth, combined);

        auto currTime = std::chrono::high_resolution_clock::now();
        double fps = 1.0 / std::chrono::duration<double>(currTime - prevTime).count();
        prevTime = currTime;

        cv::putText(combined, "C++ CUDA FPS: " + std::to_string((int)fps), Point(10, 30), FONT_HERSHEY_SIMPLEX, 1, Scalar(0, 255, 0), 2);
        cv::imshow("Depth Anything V2 (CUDA Optimized)", combined);

        if (cv::waitKey(1) == 'q') break;
    }

    cudaFree(d_input);
    cudaFree(d_output);
    cudaFree(d_raw_img);
    return 0;
}
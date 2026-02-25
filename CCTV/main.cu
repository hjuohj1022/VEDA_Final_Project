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

int main(int argc, char** argv) {
    bool headless = false;
    bool forceGui = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless" || arg == "-hl") headless = true;
        if (arg == "--gui") forceGui = true;
    }
    if (forceGui) headless = false;
    std::cout << "[APP] Mode: " << (headless ? "headless" : "gui") << std::endl;

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
    // 2. 텐서 shape 확인 + 메모리 할당
    // --------------------------------------
    auto volume = [](const Dims& dims) {
        size_t v = 1;
        for (int i = 0; i < dims.nbDims; ++i) v *= static_cast<size_t>(dims.d[i]);
        return v;
    };

    const char* inputName = nullptr;
    const char* outputName = nullptr;

    for (int i = 0; i < engine->getNbIOTensors(); ++i) {
        const char* name = engine->getIOTensorName(i);
        TensorIOMode mode = engine->getTensorIOMode(name);
        if (mode == TensorIOMode::kINPUT) {
            inputName = name;
            std::cout << "[TRT] Found Input Tensor: " << name << std::endl;
        } else if (mode == TensorIOMode::kOUTPUT) {
            outputName = name;
            std::cout << "[TRT] Found Output Tensor: " << name << std::endl;
        }
    }

    if (!inputName || !outputName) {
        std::cerr << "Error: Failed to find input/output tensor names." << std::endl;
        return -1;
    }

    Dims inputDims = context->getTensorShape(inputName);
    bool needSetInput = false;
    for (int i = 0; i < inputDims.nbDims; ++i) {
        if (inputDims.d[i] < 0) {
            needSetInput = true;
            break;
        }
    }
    if (needSetInput || inputDims.nbDims == 0) {
        Dims4 fixedInput{1, 3, INPUT_SIZE, INPUT_SIZE};
        if (!context->setInputShape(inputName, fixedInput)) {
            std::cerr << "Error: Failed to set input shape." << std::endl;
            return -1;
        }
    }

    inputDims = context->getTensorShape(inputName);
    Dims outputDims = context->getTensorShape(outputName);

    for (int i = 0; i < inputDims.nbDims; ++i) {
        if (inputDims.d[i] < 0) {
            std::cerr << "Error: Input dims unresolved after setInputShape." << std::endl;
            return -1;
        }
    }
    for (int i = 0; i < outputDims.nbDims; ++i) {
        if (outputDims.d[i] < 0) {
            std::cerr << "Error: Output dims unresolved." << std::endl;
            return -1;
        }
    }

    nvinfer1::DataType inputType = engine->getTensorDataType(inputName);
    nvinfer1::DataType outputType = engine->getTensorDataType(outputName);
    if (inputType != nvinfer1::DataType::kFLOAT || outputType != nvinfer1::DataType::kFLOAT) {
        std::cerr << "Error: Expected FP32 tensors. Input type=" << static_cast<int>(inputType)
                  << " Output type=" << static_cast<int>(outputType) << std::endl;
        return -1;
    }

    size_t inputSize = volume(inputDims) * sizeof(float);
    size_t outputElements = volume(outputDims);
    size_t outputSize = outputElements * sizeof(float);

    std::cout << "[TRT] Input Dims: ";
    for (int i = 0; i < inputDims.nbDims; ++i) std::cout << inputDims.d[i] << (i + 1 < inputDims.nbDims ? "x" : "");
    std::cout << " (" << inputSize << " bytes)" << std::endl;
    std::cout << "[TRT] Output Dims: ";
    for (int i = 0; i < outputDims.nbDims; ++i) std::cout << outputDims.d[i] << (i + 1 < outputDims.nbDims ? "x" : "");
    std::cout << " (" << outputSize << " bytes)" << std::endl;
    size_t rawInputSize = INPUT_SIZE * INPUT_SIZE * 3 * sizeof(uint8_t);

    void *d_input, *d_output, *d_raw_img;
    checkCudaErrors(cudaMalloc(&d_input, inputSize));
    checkCudaErrors(cudaMalloc(&d_output, outputSize));
    checkCudaErrors(cudaMalloc(&d_raw_img, rawInputSize));

    context->setTensorAddress(inputName, d_input);
    context->setTensorAddress(outputName, d_output);

    cudaStream_t trtStream;
    checkCudaErrors(cudaStreamCreate(&trtStream));

    // --------------------------------------
    // 3. 스트리밍 시작
    // --------------------------------------
    _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp");
    VideoCapture cap(GetSelectedRtspUrl(), CAP_FFMPEG);
    cap.set(CAP_PROP_BUFFERSIZE, 1);
    if (!cap.isOpened()) {
        std::cerr << "Error: Cannot open RTSP stream" << std::endl;
        return -1;
    }
    
    Mat frame, resized;
    std::vector<float> outputBuffer(outputElements);
    uint64_t frameIdx = 0;

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
        frameIdx++;
        if (frame.empty()) {
            std::cerr << "[WARN] Empty frame at idx=" << frameIdx << std::endl;
            continue;
        }
        if (frame.channels() != 3) {
            std::cerr << "[WARN] Unexpected channels=" << frame.channels()
                      << " at idx=" << frameIdx << std::endl;
            continue;
        }

        // 1. CPU Resize (INTER_LINEAR)
        cv::resize(frame, resized, Size(INPUT_SIZE, INPUT_SIZE), 0, 0, INTER_LINEAR);
        if (resized.empty() || resized.cols != INPUT_SIZE || resized.rows != INPUT_SIZE) {
            std::cerr << "[WARN] Resize failed or size mismatch at idx=" << frameIdx << std::endl;
            continue;
        }
        if (resized.type() != CV_8UC3) {
            std::cerr << "[WARN] Unexpected resized type=" << resized.type()
                      << " at idx=" << frameIdx << std::endl;
            continue;
        }

        // 2. Raw Image Copy (CPU -> GPU)
        if (!resized.isContinuous()) {
            resized = resized.clone();
        }
        checkCudaErrors(cudaMemcpyAsync(d_raw_img, resized.data, rawInputSize, cudaMemcpyHostToDevice, trtStream));

        // 3. CUDA Preprocess Kernel 실행 (Normalize & HWC->CHW)
        preprocess_kernel<<<grid, block, 0, trtStream>>>((uint8_t*)d_raw_img, (float*)d_input, INPUT_SIZE, INPUT_SIZE);
        auto err = cudaGetLastError();
        if (err != cudaSuccess) {
            std::cerr << "CUDA kernel error: " << cudaGetErrorString(err)
                      << " (frame=" << frameIdx << ")" << std::endl;
            break;
        }

        // 4. TensorRT 추론
        if (!context->enqueueV3(trtStream)) {
            std::cerr << "enqueueV3 failed (frame=" << frameIdx << ")" << std::endl;
            break;
        }
        err = cudaGetLastError();
        if (err != cudaSuccess) {
            std::cerr << "CUDA post-enqueue error: " << cudaGetErrorString(err)
                      << " (frame=" << frameIdx << ")" << std::endl;
            break;
        }

        // 5. 결과 회수 (GPU -> CPU)
        checkCudaErrors(cudaMemcpyAsync(outputBuffer.data(), d_output, outputSize, cudaMemcpyDeviceToHost, trtStream));
        err = cudaStreamSynchronize(trtStream);
        if (err != cudaSuccess) {
            std::cerr << "CUDA stream sync error: " << cudaGetErrorString(err)
                      << " (frame=" << frameIdx << ")" << std::endl;
            break;
        }

        // 6. 시각화 (CPU)
        int outH = INPUT_SIZE;
        int outW = INPUT_SIZE;
        if (outputDims.nbDims == 4) {
            outH = outputDims.d[2];
            outW = outputDims.d[3];
        } else if (outputDims.nbDims == 3) {
            outH = outputDims.d[1];
            outW = outputDims.d[2];
        } else if (outputDims.nbDims == 2) {
            outH = outputDims.d[0];
            outW = outputDims.d[1];
        }

        if (static_cast<size_t>(outH) * static_cast<size_t>(outW) > outputElements) {
            std::cerr << "[WARN] Output size mismatch outH*outW="
                      << (static_cast<size_t>(outH) * static_cast<size_t>(outW))
                      << " > outputElements=" << outputElements
                      << " (frame=" << frameIdx << ")" << std::endl;
            continue;
        }
        Mat depthMat(outH, outW, CV_32F, outputBuffer.data());
        double minVal, maxVal;
        cv::minMaxLoc(depthMat, &minVal, &maxVal);
        
        Mat depthNorm;
        depthMat.convertTo(depthNorm, CV_8U, 255.0 / (maxVal - minVal + 1e-5), -minVal * 255.0 / (maxVal - minVal + 1e-5));

        Mat depthColor;
        cv::applyColorMap(depthNorm, depthColor, COLORMAP_INFERNO);
        
        Mat showFrame, showDepth;
        cv::resize(frame, showFrame, Size(640, 480));
        cv::resize(depthColor, showDepth, Size(640, 480));
        
        if (!headless) {
            Mat combined;
            cv::hconcat(showFrame, showDepth, combined);

            auto currTime = std::chrono::high_resolution_clock::now();
            double fps = 1.0 / std::chrono::duration<double>(currTime - prevTime).count();
            prevTime = currTime;

            cv::putText(combined, "C++ CUDA FPS: " + std::to_string((int)fps), Point(10, 30), FONT_HERSHEY_SIMPLEX, 1, Scalar(0, 255, 0), 2);
            cv::imshow("Depth Anything V2 (CUDA Optimized)", combined);

            if (cv::waitKey(1) == 'q') break;
        }
    }

    cudaStreamDestroy(trtStream);
    cudaFree(d_input);
    cudaFree(d_output);
    cudaFree(d_raw_img);
    return 0;
}

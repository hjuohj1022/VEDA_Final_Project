#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include <chrono>
#include <thread>
#include <atomic>

// TensorRT & CUDA
#include <NvInfer.h>
#include <cuda_runtime_api.h>

// Windows networking
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

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

struct Request {
    int channel = -1;
    bool headless = false;
    bool headlessSet = false;
    bool gui = false;
    bool stop = false;
};

static std::vector<std::string> SplitTokens(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

static bool ParseInt(const std::string& s, int& out) {
    char* end = nullptr;
    long val = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = static_cast<int>(val);
    return true;
}

static Request ParseRequest(const std::string& line) {
    Request req;
    auto tokens = SplitTokens(line);
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if (t == "stop") {
            req.stop = true;
            continue;
        }
        if (t == "headless" || t == "headless=1" || t == "headless=true") {
            req.headless = true;
            req.headlessSet = true;
            continue;
        }
        if (t == "headless=0" || t == "headless=false") {
            req.headless = false;
            req.headlessSet = true;
            continue;
        }
        if (t == "gui" || t == "gui=1" || t == "gui=true") {
            req.gui = true;
            req.headlessSet = true;
            req.headless = false;
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

static void SendResponse(SOCKET client, const std::string& msg) {
    send(client, msg.c_str(), static_cast<int>(msg.size()), 0);
}

static void RunDepthWorker(int channel, bool headless, std::atomic<bool>& stopFlag) {
    std::cout << "[APP] Mode: " << (headless ? "headless" : "gui")
              << " | Channel: " << channel << std::endl;

    std::cout << "🚀 Loading Engine (CUDA Optimized Version)..." << std::endl;
    std::ifstream file(ENGINE_PATH, std::ios::binary | std::ios::ate);
    if (!file.good()) {
        std::cerr << "Error: Cannot find engine file at " << ENGINE_PATH << std::endl;
        return;
    }
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> engineData(size);
    file.read(engineData.data(), size);
    file.close();

    std::unique_ptr<IRuntime> runtime{createInferRuntime(gLogger)};
    std::unique_ptr<ICudaEngine> engine{runtime->deserializeCudaEngine(engineData.data(), size)};
    std::unique_ptr<IExecutionContext> context{engine->createExecutionContext()};

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
        return;
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
            return;
        }
    }

    inputDims = context->getTensorShape(inputName);
    Dims outputDims = context->getTensorShape(outputName);

    for (int i = 0; i < inputDims.nbDims; ++i) {
        if (inputDims.d[i] < 0) {
            std::cerr << "Error: Input dims unresolved after setInputShape." << std::endl;
            return;
        }
    }
    for (int i = 0; i < outputDims.nbDims; ++i) {
        if (outputDims.d[i] < 0) {
            std::cerr << "Error: Output dims unresolved." << std::endl;
            return;
        }
    }

    nvinfer1::DataType inputType = engine->getTensorDataType(inputName);
    nvinfer1::DataType outputType = engine->getTensorDataType(outputName);
    if (inputType != nvinfer1::DataType::kFLOAT || outputType != nvinfer1::DataType::kFLOAT) {
        std::cerr << "Error: Expected FP32 tensors. Input type=" << static_cast<int>(inputType)
                  << " Output type=" << static_cast<int>(outputType) << std::endl;
        return;
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

    _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp|stimeout;5000000|rw_timeout;5000000");
    const std::string& rtspUrl = RTSP_URLS[channel];
    VideoCapture cap(rtspUrl, CAP_FFMPEG);
    cap.set(CAP_PROP_BUFFERSIZE, 1);
    cap.set(CAP_PROP_OPEN_TIMEOUT_MSEC, 3000);
    cap.set(CAP_PROP_READ_TIMEOUT_MSEC, 3000);
    if (!cap.isOpened()) {
        std::cerr << "Error: Cannot open RTSP stream: " << rtspUrl << std::endl;
        cudaStreamDestroy(trtStream);
        cudaFree(d_input);
        cudaFree(d_output);
        cudaFree(d_raw_img);
        return;
    }

    Mat frame, resized;
    std::vector<float> outputBuffer(outputElements);
    uint64_t frameIdx = 0;

    std::cout << "Stream Started. Press 'q' to exit." << std::endl;

    dim3 block(16, 16);
    dim3 grid((INPUT_SIZE + block.x - 1) / block.x, (INPUT_SIZE + block.y - 1) / block.y);

    auto prevTime = std::chrono::high_resolution_clock::now();

    int grabFailCount = 0;
    const int grabFailLogEvery = 30;
    while (!stopFlag.load()) {
        if (!cap.grab()) {
            grabFailCount++;
            if (grabFailCount % grabFailLogEvery == 1) {
                std::cerr << "[WARN] RTSP grab failed (" << grabFailCount
                          << "x). url=" << rtspUrl << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (!cap.retrieve(frame)) continue;
        grabFailCount = 0;
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

        if (!resized.isContinuous()) {
            resized = resized.clone();
        }
        checkCudaErrors(cudaMemcpyAsync(d_raw_img, resized.data, rawInputSize, cudaMemcpyHostToDevice, trtStream));

        preprocess_kernel<<<grid, block, 0, trtStream>>>((uint8_t*)d_raw_img, (float*)d_input, INPUT_SIZE, INPUT_SIZE);
        auto err = cudaGetLastError();
        if (err != cudaSuccess) {
            std::cerr << "CUDA kernel error: " << cudaGetErrorString(err)
                      << " (frame=" << frameIdx << ")" << std::endl;
            break;
        }

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

        checkCudaErrors(cudaMemcpyAsync(outputBuffer.data(), d_output, outputSize, cudaMemcpyDeviceToHost, trtStream));
        err = cudaStreamSynchronize(trtStream);
        if (err != cudaSuccess) {
            std::cerr << "CUDA stream sync error: " << cudaGetErrorString(err)
                      << " (frame=" << frameIdx << ")" << std::endl;
            break;
        }

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

    cap.release();
    if (!headless) {
        cv::destroyAllWindows();
    }

    cudaStreamDestroy(trtStream);
    cudaFree(d_input);
    cudaFree(d_output);
    cudaFree(d_raw_img);
}

int main(int argc, char** argv) {
    int port = 9090;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--port=", 0) == 0) {
            port = std::stoi(arg.substr(7));
        }
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "socket failed" << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind failed" << std::endl;
        closesocket(server);
        WSACleanup();
        return 1;
    }

    if (listen(server, 5) == SOCKET_ERROR) {
        std::cerr << "listen failed" << std::endl;
        closesocket(server);
        WSACleanup();
        return 1;
    }

    std::cout << "[APP] Listening on port " << port << std::endl;

    std::thread worker;
    std::atomic<bool> workerStop{false};
    bool workerRunning = false;

    while (true) {
        sockaddr_in clientAddr{};
        int clientLen = sizeof(clientAddr);
        SOCKET client = accept(server, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (client == INVALID_SOCKET) continue;

        char buf[1024];
        int len = recv(client, buf, sizeof(buf) - 1, 0);
        if (len <= 0) {
            std::cout << "[APP] Client connected but sent no data" << std::endl;
            closesocket(client);
            continue;
        }
        buf[len] = '\0';

        std::string line(buf);
        {
            char ip[64] = {0};
            inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
            std::cout << "[APP] Request from " << ip << ":" << ntohs(clientAddr.sin_port)
                      << " -> " << line << std::endl;
        }
        Request req = ParseRequest(line);

        if (req.channel < -1 || req.channel > 3) {
            std::cout << "[APP] Invalid channel request" << std::endl;
            SendResponse(client, "ERR invalid channel\n");
            closesocket(client);
            continue;
        }

        if (workerRunning) {
            workerStop.store(true);
            if (worker.joinable()) worker.join();
            workerRunning = false;
        }

        if (req.stop) {
            std::cout << "[APP] Stop request processed" << std::endl;
            SendResponse(client, "OK stopped\n");
            closesocket(client);
            continue;
        }

        int channel = req.channel >= 0 ? req.channel : RTSP_CHANNEL;
        bool headless = req.headlessSet ? req.headless : false;
        if (req.gui) headless = false;

        workerStop.store(false);
        worker = std::thread([channel, headless, &workerStop]() {
            RunDepthWorker(channel, headless, workerStop);
        });
        workerRunning = true;

        std::string modeStr = headless ? "headless" : "gui";
        SendResponse(client, "OK started channel=" + std::to_string(channel) + " mode=" + modeStr + "\n");
        closesocket(client);
    }

    if (workerRunning) {
        workerStop.store(true);
        if (worker.joinable()) worker.join();
    }
    closesocket(server);
    WSACleanup();
    return 0;
}
